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
    pathlib.Path("src/json/json.cppm"),
    pathlib.Path("src/json/json.cpp"),
}


def main() -> int:
    violations: list[str] = []
    tracked = subprocess.run(
        ["git", "ls-files", "-z", "--", *SOURCE_ROOTS],
        cwd=ROOT,
        check=True,
        capture_output=True,
    ).stdout.decode("utf-8").split("\0")
    for name in tracked:
        relative = pathlib.Path(name)
        if not name or relative.suffix.lower() not in SOURCE_SUFFIXES:
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
