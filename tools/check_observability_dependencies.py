#!/usr/bin/env python3
"""Reject forbidden or needlessly broad named-module dependencies.

This source-level guard conservatively includes all preprocessor branches and
implementation units. It is not a C++ preprocessor, header dependency scanner,
linker audit, or performance measurement.
"""

from collections import deque
from pathlib import Path
import re
import sys


LITERALS = re.compile(
    r'//[^\n]*|/\*.*?\*/|(?:u8|u|U|L)?R"([^ ()\\\t\r\n]{0,16})\(.*?\)\1"'
    r'|"(?:\\.|[^"\\])*"|\'(?:\\.|[^\'\\])*\'', re.DOTALL
)
DECLARATION = re.compile(r'\b(?:export\s+)?module\s+(cnetmod[\w.:]*)\s*;')
IMPORT = re.compile(r'\b(?:export\s+)?import\s+(:?[a-zA-Z_]\w*(?:[.:]\w+)*)\s*;')
LOWER_LAYERS = (
    'cnetmod.protocol', 'cnetmod.database', 'cnetmod.core',
    'cnetmod.coro', 'cnetmod.io', 'cnetmod.executor', 'cnetmod.instrumentation',
)
UPPER_LAYERS = ('cnetmod.observability', 'cnetmod.application')
OBSERVABILITY_UMBRELLA = 'cnetmod.observability'
OPENAI_PRIMARY = 'cnetmod.protocol.openai'
OPENAI_MODEL_UMBRELLA = 'cnetmod.protocol.openai:model'
OPENAI_RUN_ONLY_PARTITIONS = {
    f'{OPENAI_PRIMARY}:{name}' for name in (
        'agentic', 'ingestion', 'loaders', 'methods', 'planners',
        'skills', 'structured', 'tools',
    )
}


def belongs_to(module, roots):
    return any(module == root or module.startswith((root + '.', root + ':'))
               for root in roots)


def parse_unit(source):
    # Preserve line numbers while excluding comments and string contents.
    source = source.replace('\\\n', '')
    clean = LITERALS.sub(lambda match: '\n' * match[0].count('\n') + ' ', source)
    declaration = DECLARATION.search(clean)
    if declaration is None:
        return None, set()
    owner = declaration[1]
    primary = owner.split(':')[0]
    imports = {primary + name if name.startswith(':') else name
               for name in IMPORT.findall(clean)}
    return owner, imports


def module_graph(source_root):
    graph = {}
    for path in sorted(source_root.rglob('*')):
        if path.suffix not in ('.cppm', '.cpp') or not path.is_file():
            continue
        owner, imports = parse_unit(path.read_text(encoding='utf-8-sig'))
        if owner:
            graph.setdefault(owner, set()).update(imports)
    if not graph:
        raise ValueError(f'No named cnetmod modules found under {source_root}')
    return graph


def violations(graph):
    """Return one shortest forbidden dependency chain per lower-layer module."""
    failures = []
    for origin in sorted(graph):
        if not belongs_to(origin, LOWER_LAYERS):
            continue
        queue = deque([(origin,)])
        visited = {origin}
        while queue:
            chain = queue.popleft()
            if belongs_to(chain[-1], UPPER_LAYERS):
                failures.append(chain)
                break
            for target in sorted(graph.get(chain[-1], ())):
                if target not in visited:
                    visited.add(target)
                    queue.append((*chain, target))
    return failures


def broad_observability_imports(graph):
    """Reject Application internals that pull every observability adapter."""
    return sorted(owner for owner, imports in graph.items()
                  if belongs_to(owner, ('cnetmod.application',))
                  and OBSERVABILITY_UMBRELLA in imports)


def broad_openai_model_imports(graph):
    """Keep model contracts out of lifecycle-only OpenAI units."""
    return sorted(owner for owner, imports in graph.items()
                  if owner in OPENAI_RUN_ONLY_PARTITIONS
                  and OPENAI_MODEL_UMBRELLA in imports)


def main():
    root = Path(__file__).resolve().parent.parent / 'src'
    try:
        graph = module_graph(root)
    except (OSError, ValueError) as error:
        print(f'Module dependency check failed: {error}', file=sys.stderr)
        return 1
    failures = violations(graph)
    for chain in failures:
        print('Forbidden module dependency: ' + ' -> '.join(chain), file=sys.stderr)
    broad_imports = broad_observability_imports(graph)
    for owner in broad_imports:
        print(f'Broad observability import: {owner} -> '
              f'{OBSERVABILITY_UMBRELLA}; import the required narrow module',
              file=sys.stderr)
    broad_model_imports = broad_openai_model_imports(graph)
    for owner in broad_model_imports:
        print(f'Broad OpenAI model import: {owner} -> '
              f'{OPENAI_MODEL_UMBRELLA}; import the required narrow partition',
              file=sys.stderr)
    if failures or broad_imports or broad_model_imports:
        return 1
    print(f'Checked {len(graph)} named modules: architecture boundaries are valid.')
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
