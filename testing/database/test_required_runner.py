import contextlib
import io
import os
from types import SimpleNamespace
import unittest
from unittest import mock

import run_optional_pytest as runner


class RequiredRunnerTests(unittest.TestCase):
    def test_missing_dependencies(self):
        for required, expected in (("0", 77), ("1", 1)):
            with self.subTest(required=required), \
                 mock.patch.dict(os.environ, {"CNETMOD_DATABASE_REQUIRED": required}), \
                 mock.patch.object(runner.sys, "argv", ["runner", "--config", "pytest.ini", "postgresql"]), \
                 mock.patch.object(runner.importlib.util, "find_spec", return_value=None), \
                 contextlib.redirect_stdout(io.StringIO()):
                self.assertEqual(runner.main(), expected)

    def test_execution_skip_fails_required_run(self):
        plugin = runner.RequiredRun()
        plugin.pytest_runtest_logreport(SimpleNamespace(skipped=True))
        session = SimpleNamespace(exitstatus=0)
        plugin.pytest_sessionfinish(session, 0)
        self.assertEqual(session.exitstatus, 1)

    def test_collection_skip_fails_required_run(self):
        plugin = runner.RequiredRun()
        plugin.pytest_collectreport(SimpleNamespace(skipped=True))
        session = SimpleNamespace(exitstatus=0)
        plugin.pytest_sessionfinish(session, 0)
        self.assertEqual(session.exitstatus, 1)

    def test_success_and_existing_failure_preserved(self):
        plugin = runner.RequiredRun()
        for status in (0, 2, 5):
            session = SimpleNamespace(exitstatus=status)
            plugin.pytest_sessionfinish(session, status)
            self.assertEqual(session.exitstatus, status)


if __name__ == "__main__":
    unittest.main()
