#!/usr/bin/env python3
"""Generate the repository AGENTS.md from all Markdown files under skill/."""

from __future__ import annotations

import argparse
from pathlib import Path
import sys


REPOSITORY_ROOT = Path(__file__).resolve().parent.parent
SKILL_ROOT = REPOSITORY_ROOT / "skill"
OUTPUT_PATH = REPOSITORY_ROOT / "AGENTS.md"


def normalize_markdown(path: Path) -> str:
    """Read UTF-8 Markdown and normalize its line endings and trailing newline."""
    content = path.read_text(encoding="utf-8")
    content = content.replace("\r\n", "\n").replace("\r", "\n")
    return content.rstrip("\n") + "\n"


def source_files() -> list[Path]:
    """Return all skill Markdown files in a platform-independent stable order."""
    files = [path for path in SKILL_ROOT.rglob("*.md") if path.is_file()]
    return sorted(
        files,
        key=lambda path: (
            path != SKILL_ROOT / "SKILL.md",
            path.relative_to(REPOSITORY_ROOT).as_posix().casefold(),
        ),
    )


def generate() -> str:
    files = source_files()
    if not files:
        raise RuntimeError(f"no Markdown files found under {SKILL_ROOT}")

    relative_paths = [
        path.relative_to(REPOSITORY_ROOT).as_posix() for path in files
    ]
    sections = [
        "<!-- GENERATED FILE: DO NOT EDIT DIRECTLY. -->\n",
        "# cnetmod Repository Instructions\n",
        "This file is generated from every `skill/**/*.md` file. "
        "Edit the source files and run `python tools/generate_agents.py`; "
        "use `python tools/generate_agents.py --check` to verify it is current.\n",
        "## Included Sources\n",
        *(f"- `{relative_path}`\n" for relative_path in relative_paths),
    ]

    for path, relative_path in zip(files, relative_paths):
        sections.extend(
            [
                f"\n<!-- BEGIN SOURCE: {relative_path} -->\n",
                f"# Source: `{relative_path}`\n\n",
                normalize_markdown(path),
                f"<!-- END SOURCE: {relative_path} -->\n",
            ]
        )

    return "".join(sections)


def parse_arguments() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate AGENTS.md from all Markdown files under skill/."
    )
    parser.add_argument(
        "--check",
        action="store_true",
        help="exit with a failure status when AGENTS.md is missing or stale",
    )
    return parser.parse_args()


def main() -> int:
    arguments = parse_arguments()
    try:
        generated = generate()
    except (OSError, RuntimeError, UnicodeError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 2

    current = None
    if OUTPUT_PATH.exists():
        current = normalize_markdown(OUTPUT_PATH)

    if arguments.check:
        if current != generated:
            print(
                "AGENTS.md is stale; run: python tools/generate_agents.py",
                file=sys.stderr,
            )
            return 1
        print("AGENTS.md is up to date")
        return 0

    if current == generated:
        print("AGENTS.md is already up to date")
        return 0

    OUTPUT_PATH.write_text(generated, encoding="utf-8", newline="\n")
    print(f"generated {OUTPUT_PATH} from {len(source_files())} skill files")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
