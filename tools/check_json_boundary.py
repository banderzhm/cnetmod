#!/usr/bin/env python3
"""Verify that direct Glaze use stays inside the cnetmod.json module."""

from __future__ import annotations

import pathlib
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
    source_files = sorted(
        path
        for root in SOURCE_ROOTS
        for path in (ROOT / root).rglob("*")
        if path.is_file() and path.suffix.lower() in SOURCE_SUFFIXES
    )
    for path in source_files:
        relative = path.relative_to(ROOT)
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
