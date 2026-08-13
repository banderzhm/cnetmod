"""Run an optional database pytest suite with CTest-compatible skips."""

from __future__ import annotations

import argparse
import importlib.util
import sys
from pathlib import Path


SKIP_RETURN_CODE = 77


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--config", required=True)
    parser.add_argument("--module", action="append", default=[])
    parser.add_argument("target")
    arguments = parser.parse_args()

    # CTest invokes this runner by absolute path, so Python puts
    # ``testing/database`` rather than the test working directory on
    # ``sys.path``.  Example suites import their shared helpers through the
    # working-directory package (for example ``database.common``).
    sys.path.insert(0, str(Path.cwd()))

    required = ("pytest", *arguments.module)
    missing = [name for name in required if importlib.util.find_spec(name) is None]
    if missing:
        print("SKIP: database interoperability dependencies are not installed: " + ", ".join(missing))
        return SKIP_RETURN_CODE

    import pytest

    return pytest.main(["-c", arguments.config, arguments.target])


if __name__ == "__main__":
    raise SystemExit(main())
