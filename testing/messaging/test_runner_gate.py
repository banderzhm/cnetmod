"""Dependency-free checks for mandatory CI interoperability gates."""

from __future__ import annotations

import contextlib
import io
import os
import sys
import types
import unittest
from unittest.mock import Mock, patch

import run_messaging_interoperability as runner


class RunnerGateTests(unittest.TestCase):
    def run_gate(self, required: bool, mode: str, *, missing=False, docker=False):
        environment = {
            "CNETMOD_MESSAGING_REQUIRED": "1" if required else "0",
            "CNETMOD_MESSAGING_SERVICE_MODE": mode,
        }
        modules = {
            "dotenv": types.SimpleNamespace(load_dotenv=lambda *a, **kw: None),
            "pytest": types.SimpleNamespace(main=lambda arguments, **kwargs: 3),
        }
        output = io.StringIO()
        with (
            patch.dict(os.environ, environment, clear=True),
            patch.object(sys, "argv", ["runner", "kafka"]),
            patch.object(runner.importlib.util, "find_spec", return_value=None if missing else object()),
            patch.object(runner, "_docker_is_usable", return_value=docker),
            patch.dict(sys.modules, modules),
            contextlib.redirect_stdout(output),
        ):
            result = runner.main()
        return result, output.getvalue()

    def test_missing_dependencies_fail_only_when_required(self):
        for required in (False, True):
            with self.subTest(required=required):
                result, output = self.run_gate(required, "container", missing=True)
                self.assertEqual(result, 1 if required else 77)
                self.assertTrue(output.startswith("ERROR:" if required else "SKIP:"))

    def test_missing_infrastructure_cannot_silently_pass_ci(self):
        for mode in ("auto", "container", "external"):
            for required in (False, True):
                with self.subTest(mode=mode, required=required):
                    result, _ = self.run_gate(required, mode)
                    self.assertEqual(result, 1 if required else 77)

    def test_pytest_failure_is_preserved(self):
        result, _ = self.run_gate(True, "container", docker=True)
        self.assertEqual(result, 3)

    def test_required_execution_rejects_skipped_tests(self):
        gate = runner._RequiredExecutionGate()
        gate.pytest_runtest_logreport(types.SimpleNamespace(skipped=True))
        session = types.SimpleNamespace(exitstatus=0)
        gate.pytest_sessionfinish(session, 0)
        self.assertEqual(session.exitstatus, 1)

    def test_optional_execution_preserves_success_with_skips(self):
        gate = runner._RequiredExecutionGate()
        gate.pytest_collectreport(types.SimpleNamespace(skipped=True))
        session = types.SimpleNamespace(exitstatus=0)
        gate.pytest_sessionfinish(session, 2)
        self.assertEqual(session.exitstatus, 0)

    def test_docker_probe_always_closes_created_client(self):
        for failed in (False, True):
            with self.subTest(failed=failed):
                client = Mock()
                if failed:
                    client.ping.side_effect = RuntimeError("unavailable")
                docker = types.SimpleNamespace(from_env=lambda: client)
                with patch.dict(sys.modules, {"docker": docker}):
                    self.assertEqual(runner._docker_is_usable(), not failed)
                client.close.assert_called_once_with()

    def test_docker_construction_failure_is_unavailable(self):
        factory = Mock(side_effect=RuntimeError("unavailable"))
        with patch.dict(sys.modules, {"docker": types.SimpleNamespace(from_env=factory)}):
            self.assertFalse(runner._docker_is_usable())


if __name__ == "__main__":
    unittest.main()
