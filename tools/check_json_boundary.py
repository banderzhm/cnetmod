#!/usr/bin/env python3
"""Verify that direct Glaze use stays inside the cnetmod.json module."""

from __future__ import annotations

import pathlib
import subprocess
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
SOURCE_ROOTS = ("src", "include", "testing", "examples")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".cxx", ".h", ".hpp", ".cppm", ".ixx"}
GLAZE_MODULE_FILES = {
    pathlib.Path("src/utils/json/json.cppm"),
    pathlib.Path("src/utils/json/json.cpp"),
}


def source_files() -> list[pathlib.Path]:
    """Return repository sources even when a Windows worktree is built in WSL.

    Git writes an absolute Windows ``gitdir`` into the worktree's ``.git``
    file. Linux Git cannot resolve that path, so build-time boundary checks
    must not depend exclusively on ``git ls-files``.
    """
    listed = subprocess.run(
        ["git", "ls-files", "-z", "--", *SOURCE_ROOTS],
        cwd=ROOT,
        check=False,
        capture_output=True,
    )
    if listed.returncode == 0:
        return [
            pathlib.Path(name)
            for name in listed.stdout.decode("utf-8").split("\0")
            if name
        ]

    return sorted(
        path.relative_to(ROOT)
        for source_root in SOURCE_ROOTS
        for path in (ROOT / source_root).rglob("*")
        if path.is_file()
    )


def main() -> int:
    violations: list[str] = []
    for relative in source_files():
        if relative.suffix.lower() not in SOURCE_SUFFIXES:
            continue
        path = ROOT / relative
        if not path.is_file():
            continue
        text = path.read_text(encoding="utf-8", errors="replace")
        if "nlohmann" in text:
            violations.append(f"{relative}: nlohmann bypasses cnetmod.json")
        if relative not in GLAZE_MODULE_FILES and (
            "#include <glaze/" in text or "glz::" in text
        ):
            violations.append(
                f"{relative}: direct Glaze use belongs in cnetmod.json"
            )

    if violations:
        print("JSON abstraction boundary violations:", file=sys.stderr)
        for violation in violations:
            print(f"  - {violation}", file=sys.stderr)
        return 1
    print("JSON abstraction boundary verified")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
