#!/usr/bin/env python3
"""Verify that ORM documentation follows the current public architecture."""

from __future__ import annotations

import pathlib
import re
import sys


ROOT = pathlib.Path(__file__).resolve().parents[1]
API_SOURCE = ROOT / "src/application/orm_repository.cppm"
MAPPER_SOURCE = ROOT / "src/database/orm/mapper.cppm"
API_DOCUMENT = ROOT / "skill/database/database-orm.md"
SCAN_ROOTS = (ROOT / "skill", ROOT / "docs", ROOT / "README.md")

LEGACY_IDENTIFIERS = (
    "mysql_session",
    "postgresql_session",
    "mysql_database_session",
    "postgresql_database_session",
    "session_repository",
    "base_mapper",
    "mysql_orm_service",
    "postgresql_orm_service",
    "runtime.mysql_repository",
    "runtime.postgresql_repository",
    "unit.xml<",
    "mysql_select<",
    "mysql_pageable_mapper",
    "mysql_mapper_session",
    "mysql_synchronize_schema",
    ".find_all<",
    ".find_by_id<",
)


def public_methods(source: str, class_name: str) -> set[str]:
    start = source.index(f"class {class_name}")
    public = source[start:]
    public = public[: public.index("\nprivate:")]
    return set(
        re.findall(
            r"^\s+(?:\[\[nodiscard\]\]\s+)?auto\s+(\w+)\s*\(",
            public,
            re.MULTILINE,
        )
    )


def documented_methods(document: str, section: str) -> set[str]:
    begin = f"<!-- {section}_BEGIN -->"
    end = f"<!-- {section}_END -->"
    if begin not in document or end not in document:
        raise ValueError("ORM repository API markers are missing")
    table = document.split(begin, 1)[1].split(end, 1)[0]
    return set(re.findall(r"^\| `([a-z_]+)` \|", table, re.MULTILINE))


def markdown_files() -> list[pathlib.Path]:
    files: list[pathlib.Path] = []
    for root in SCAN_ROOTS:
        if root.is_file():
            files.append(root)
        else:
            files.extend(root.rglob("*.md"))
    return files


def main() -> int:
    errors: list[str] = []
    document = API_DOCUMENT.read_text(encoding="utf-8")
    contracts = (
        ("application_repository", API_SOURCE, "ORM_REPOSITORY_API"),
        ("mapper", MAPPER_SOURCE, "ORM_MAPPER_API"),
    )
    checked_methods = 0
    for class_name, source_path, section in contracts:
        source_methods = public_methods(
            source_path.read_text(encoding="utf-8"), class_name
        )
        documented = documented_methods(document, section)
        checked_methods += len(source_methods)
        missing = sorted(source_methods - documented)
        stale = sorted(documented - source_methods)
        if missing:
            errors.append(
                f"undocumented {class_name} methods: " + ", ".join(missing)
            )
        if stale:
            errors.append(
                f"documented methods absent from {class_name}: "
                + ", ".join(stale)
            )

    for path in markdown_files():
        text = path.read_text(encoding="utf-8")
        for identifier in LEGACY_IDENTIFIERS:
            if identifier not in text:
                continue
            for line_number, line in enumerate(text.splitlines(), 1):
                if identifier in line:
                    relative = path.relative_to(ROOT)
                    errors.append(
                        f"{relative}:{line_number}: legacy ORM identifier {identifier!r}"
                    )

    if errors:
        print("ORM documentation validation failed:")
        for error in errors:
            print(f"- {error}")
        return 1

    print(
        "ORM documentation matches application_repository and mapper APIs "
        f"({checked_methods} methods); no legacy identifiers found."
    )
    return 0


if __name__ == "__main__":
    sys.exit(main())
