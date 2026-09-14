"""Run CTest and reject a nominally successful run containing skipped tests."""

from __future__ import annotations

import subprocess
import sys
import tempfile
import xml.etree.ElementTree as element_tree
from pathlib import Path


def main() -> int:
    with tempfile.TemporaryDirectory(prefix="cnetmod-ctest-") as directory:
        report = Path(directory) / "results.xml"
        completed = subprocess.run(
            ["ctest", *sys.argv[1:], "--output-junit", str(report)], check=False
        )
        if completed.returncode != 0:
            return completed.returncode
        root = element_tree.parse(report).getroot()
        skipped = [
            case.attrib.get("name", "<unnamed>")
            for case in root.iter("testcase")
            if case.find("skipped") is not None
        ]
        if skipped:
            print(
                "ERROR: required CTest run skipped tests: " + ", ".join(skipped),
                file=sys.stderr,
            )
            return 1
        return 0


if __name__ == "__main__":
    raise SystemExit(main())
