import contextlib
import io
import os
import sys
from types import SimpleNamespace
import unittest
from unittest import mock

import check_mysql_live_configuration as checker


class MySQLConfigurationCheckTests(unittest.TestCase):
    def completion_output(self):
        return (f"[  FAILED  ] {checker.SCENARIO} (1 failure(s))\n"
                "[==========] 1 test(s) ran.\n"
                "[  PASSED  ] 0 test(s).\n"
                "[  FAILED  ] 1 test(s).\n").encode()

    def test_incomplete_or_wrong_execution_is_not_completion(self):
        output = self.completion_output()
        for candidate in (b"", checker.SCENARIO.encode(),
                          f"[ RUN      ] {checker.SCENARIO}\n".encode(),
                          output.replace(b"1 test(s) ran.", b"2 test(s) ran."),
                          output.replace(checker.SCENARIO.encode(), b"another_test")):
            with self.subTest(candidate=candidate):
                self.assertFalse(checker.completed_failure(SimpleNamespace(returncode=1, stdout=candidate)))
        for code in (0, 77, -1):
            self.assertFalse(checker.completed_failure(SimpleNamespace(returncode=code, stdout=output)))
        self.assertTrue(checker.completed_failure(SimpleNamespace(returncode=1, stdout=output)))

    def test_watchdog_selects_its_scenario_even_with_an_inherited_filter(self):
        scenario = "mysql_live_authentication_health_and_supervised_stop"
        results = [SimpleNamespace(returncode=1, stdout=b"", stderr=b"")
                   for _ in range(7)]
        results.append(SimpleNamespace(returncode=1, stdout=self.completion_output(), stderr=b""))
        with mock.patch.dict(os.environ, {"CNETMOD_TEST_FILTER": "unrelated"}), \
             mock.patch.object(sys, "argv", ["checker", "test-executable"]), \
             mock.patch.object(checker.subprocess, "run", side_effect=results) as run, \
             contextlib.redirect_stdout(io.StringIO()):
            checker.main()
        self.assertEqual(run.call_count, 8)
        for call in run.call_args_list:
            self.assertEqual(call.args[0], ["test-executable"])
            self.assertEqual(call.kwargs["env"]["CNETMOD_TEST_FILTER"], scenario)
        self.assertEqual(run.call_args_list[-1].kwargs["timeout"], 15)


if __name__ == "__main__":
    unittest.main()
