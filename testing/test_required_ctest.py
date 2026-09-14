from __future__ import annotations

import sys
import unittest
from unittest.mock import patch

import run_required_ctest as runner

class RequiredCTestTests(unittest.TestCase):
    def test_success_without_skips(self):
        self.assertEqual(self._run("<testsuite><testcase name='ok'/></testsuite>"), 0)

    def test_skip_is_failure(self):
        self.assertEqual(
            self._run("<testsuite><testcase name='missing'><skipped/></testcase></testsuite>"),
            1,
        )

    def _run(self, report: str) -> int:
        def execute(arguments, check):
            report_path = arguments[arguments.index("--output-junit") + 1]
            with open(report_path, "w", encoding="utf-8") as destination:
                destination.write(report)
            return type("Completed", (), {"returncode": 0})()

        with patch.object(sys, "argv", ["run_required_ctest.py"]), patch.object(
            runner.subprocess, "run", side_effect=execute
        ):
            return runner.main()


if __name__ == "__main__":
    unittest.main()
