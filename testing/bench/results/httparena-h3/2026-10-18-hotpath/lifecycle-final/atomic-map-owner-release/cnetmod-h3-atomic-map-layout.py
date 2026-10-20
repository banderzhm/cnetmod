"""Prepare/run Clang's real private-entry layout diagnostic (no ABI hooks).

Only execute in an assigned build window with a verified normal current build.
This is a syntax-only external invocation, not a runtime sizeof mirror model.
It instantiates the real server inbox map from the actual source and PCM set.
"""
from pathlib import Path
import argparse
import hashlib
import importlib.util
import json
import subprocess
import sys


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--repo', type=Path, default=Path('/mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod'))
    parser.add_argument('--build', type=Path, default=Path('/var/tmp/cnetmod-h3-arena-build'))
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--current-build-verified', action='store_true', required=True)
    parser.add_argument('--bind-recorded-build', type=Path, help=argparse.SUPPRESS)
    args = parser.parse_args()
    build, repo, out = args.build.resolve(), args.repo.resolve(), args.out.resolve()
    if out.exists() or repo in out.parents or build in out.parents:
        parser.error('--out must be new and outside source/build')
    entries = json.loads((build / 'compile_commands.json').read_text())
    entry = next(item for item in entries if Path(item['file']).name == 'http3_server.cpp')
    recorded = Path(entry['directory'])
    if args.bind_recorded_build:
        if args.bind_recorded_build != recorded:
            raise RuntimeError('recorded path changed')
        recorded.mkdir(parents=True, exist_ok=True)
        subprocess.run(['mount', '--bind', str(build), str(recorded)], check=True)
        subprocess.run(['mount', '-o', 'remount,bind,ro', str(recorded)], check=True)
    else:
        raise SystemExit(subprocess.run([
            'unshare', '--mount', '--propagation', 'private', sys.executable,
            str(Path(__file__).resolve()), *sys.argv[1:],
            '--bind-recorded-build', str(recorded)]).returncode)
    spec = importlib.util.spec_from_file_location('cold_helpers',
        Path(__file__).with_name('cnetmod-h3-close-diagnostic-latest-build.py'))
    helpers = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helpers)
    expanded = helpers.expand(helpers.tokens(entry), recorded)
    command = []
    skip = False
    for arg in expanded:
        if skip:
            skip = False
            continue
        if arg in ('-o', '-MF', '-MT', '-MQ', '-MJ', '-serialize-diagnostics'):
            skip = True
            continue
        if arg in ('-c', '-MD', '-MMD', '-MP'):
            continue
        command.append(arg)
    command += ['-fsyntax-only', '-Xclang', '-fdump-record-layouts']
    paths = set(build.rglob('*.pcm')) | set(build.rglob('*.modmap')) | set(build.glob('*.a'))
    paths |= {Path(entry['file']), repo / 'src/utils/concurrentContainers/atomic_hash_map.cppm',
              repo / 'src/utils/concurrentContainers/atomic_hash_map.inl'}
    def snap():
        return {str(path): hashlib.sha256(path.read_bytes()).hexdigest()
                for path in sorted(paths) if path.is_file()}
    before = snap()
    out.mkdir(parents=True)
    (out / 'command.json').write_text(json.dumps(command, indent=2))
    result = subprocess.run(command, cwd=recorded, text=True,
                            stdout=subprocess.PIPE, stderr=subprocess.STDOUT)
    (out / 'complete-layout.log').write_text(result.stdout)
    blocks = result.stdout.split('*** Dumping AST Record Layout')
    relevant = [block for block in blocks if 'atomic_hash_map<' in block
                and ('::entry' in block or '::slot' in block)]
    (out / 'atomic-map-layout.log').write_text(
        '\n'.join('*** Dumping AST Record Layout' + block for block in relevant))
    after = snap()
    (out / 'integrity.json').write_text(json.dumps({
        'unchanged': before == after, 'before': before, 'after': after}, indent=2))
    assert result.returncode == 0 and relevant, 'compiler layout diagnostic failed or emitted no real entries'
    assert before == after, 'production inputs changed'


if __name__ == '__main__':
    main()
