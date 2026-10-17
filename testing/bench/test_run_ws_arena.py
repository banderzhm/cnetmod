"""Low-load checks: reject unusable measurements instead of reporting zeroes."""
import json
from pathlib import Path
from types import SimpleNamespace
import tempfile
import unittest
from unittest.mock import patch

import run_ws_arena as runner


class RunnerTests(unittest.TestCase):
    def invoke(self, result):
        args = SimpleNamespace(client_cpus="8-11", gcannon="/gcannon", port=19090, threads=4)
        with tempfile.TemporaryDirectory() as directory:
            output = Path(directory) / "sample.json"

            def generate(command, **kwargs):
                kwargs["stdout"].write(json.dumps(result))

            with patch.object(runner.subprocess, "run", side_effect=generate):
                return runner.run_client(args, 512, 16, 0, 5, 5, output)

    def valid(self):
        return {"mode": "websocket", "responses": 100, "connections": 512,
                "threads": 4, "pipeline_depth": 16, "latency_us": {"samples": 100}}

    def test_no_responses_cannot_pass(self):
        sample = self.valid()
        sample["responses"] = 0
        with self.assertRaisesRegex(RuntimeError, "no WebSocket responses"):
            self.invoke(sample)

    def test_http_responses_cannot_pass_as_websocket(self):
        sample = self.valid()
        sample["mode"] = "http"
        with self.assertRaisesRegex(RuntimeError, "no WebSocket responses"):
            self.invoke(sample)

    def test_missing_latency_cannot_pass(self):
        sample = self.valid()
        sample.pop("latency_us")
        with self.assertRaisesRegex(RuntimeError, "missing latency"):
            self.invoke(sample)

    def test_changed_concurrency_cannot_pass(self):
        sample = self.valid()
        sample["pipeline_depth"] = 1
        with self.assertRaisesRegex(RuntimeError, "changed pipeline_depth"):
            self.invoke(sample)

    def test_command_keeps_explicit_workload(self):
        result, command = self.invoke(self.valid())
        self.assertEqual(result["responses"], 100)
        self.assertEqual(command[-1], "--json")
        self.assertEqual(command[command.index("--ws-msg") + 1], "aaaaa")
        self.assertEqual(command[command.index("-p") + 1], "16")

    def test_process_comm_does_not_shift_cpu_accounting(self):
        # A process name may contain spaces and closing parentheses.
        with patch.object(Path, "read_text", return_value="42 (ws worker)) S 1 2 3 4 5 6 7 8 9 10 120 30 0"):
            with patch.object(runner.os, "sysconf", return_value=100):
                self.assertEqual(runner.cpu_seconds(42), 1.5)


if __name__ == "__main__":
    unittest.main()
