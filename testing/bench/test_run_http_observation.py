import contextlib
import http.server
import io
import subprocess
import tempfile
import unittest
from pathlib import Path
from unittest import mock

import run_http_observation as runner


def complete_output():
    return "\n".join(
        f"round={index} mode={mode} requests=1000 rps=1000 p50_us=1 p99_us=2"
        for index in range(8) for mode in ("raw", "disabled")
    )


class RunnerTests(unittest.TestCase):
    def test_cpu_affinity_requires_acknowledgement(self):
        with mock.patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess(
                [], 0, "benchmark_cpu=2\n" + complete_output(), "")) as execute, \
             contextlib.redirect_stderr(io.StringIO()):
            report = runner.run(__file__, 8, cpu=2)
            self.assertEqual(report["client_cpu"], 2)
            self.assertTrue(all(call.args[0][-1] == "2" for call in execute.call_args_list))
        for acknowledgement in ("", "benchmark_cpu=3\n", "benchmark_cpu=2\nbenchmark_cpu=2\n"):
            with mock.patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess(
                    [], 0, acknowledgement + complete_output(), "")), self.assertRaises(ValueError):
                runner.run(__file__, 8, cpu=2)

    def test_invalid_cpu_rejected_before_execution(self):
        for cpu in (-1, 65536, True, "2"):
            with mock.patch.object(runner.subprocess, "run") as execute, self.assertRaises(ValueError):
                runner.run(__file__, 8, cpu=cpu)
            execute.assert_not_called()

    def test_historical_process_pairs(self):
        candidate = str(Path(__file__).resolve())
        baseline = str(Path(runner.__file__).resolve())
        raw_output = "\n".join(line for line in complete_output().splitlines() if "mode=raw" in line)

        def execute(command, **kwargs):
            output = raw_output if command[0] == baseline else complete_output().replace("rps=1000", "rps=2000")
            return subprocess.CompletedProcess(command, 0, output, "")

        with mock.patch.object(runner.subprocess, "run", side_effect=execute) as process, \
             contextlib.redirect_stderr(io.StringIO()):
            report = runner.run(candidate, 8, baseline)
        self.assertEqual(process.call_count, 16)
        self.assertEqual([call.args[0][0] for call in process.call_args_list[:4]],
                         [baseline, candidate, candidate, baseline])
        self.assertAlmostEqual(report["analysis"]["disabled_over_raw_rps"], 2.0)
        self.assertEqual(report["baseline_executable"], baseline)
        self.assertEqual(len(report["baseline_sha256"]), 64)
        self.assertEqual(len(report["runs"][0]["baseline_rows"]), 8)
        self.assertIn("provenance", report["analysis"]["scope"])

    def test_rejects_invalid_baseline_output(self):
        raw_output = "\n".join(line for line in complete_output().splitlines() if "mode=raw" in line)
        for output in (complete_output(), raw_output.rsplit("\n", 1)[0],
                       raw_output.replace("round=7", "round=6")):
            with self.subTest(output=output), \
                 mock.patch.object(runner.subprocess, "run", return_value=subprocess.CompletedProcess([], 0, output, "")), \
                 self.assertRaises(ValueError):
                runner.run(__file__, 8, runner.__file__)

    def test_rejects_same_executable_as_baseline(self):
        with self.assertRaises(ValueError):
            runner.run(__file__, 8, __file__)

    def test_rejects_cross_build_request_mismatch(self):
        raw_output = "\n".join(line for line in complete_output().splitlines() if "mode=raw" in line)
        raw_output = raw_output.replace("round=0 mode=raw requests=1000", "round=0 mode=raw requests=999")
        raw_output = raw_output.replace("round=1 mode=raw requests=1000", "round=1 mode=raw requests=1001")
        results = [subprocess.CompletedProcess([], 0, output, "") for output in (raw_output, complete_output())]
        with mock.patch.object(runner.subprocess, "run", side_effect=results), self.assertRaises(ValueError):
            runner.run(__file__, 8, runner.__file__)

    def test_equal_workload_candidate_requires_baseline(self):
        outputs = {mode: "\n".join(line for line in complete_output().splitlines() if f"mode={mode}" in line)
                   for mode in ("raw", "disabled")}
        baseline = str(Path(runner.__file__).resolve())

        def execute(command, **kwargs):
            mode = "raw" if command[0] == baseline else "disabled"
            return subprocess.CompletedProcess(command, 0, outputs[mode], "")

        with mock.patch.object(runner.subprocess, "run", side_effect=execute), \
             contextlib.redirect_stderr(io.StringIO()):
            report = runner.run(__file__, 8, baseline)
            self.assertEqual(len(report["runs"][0]["rows"]), 8)
            self.assertAlmostEqual(report["analysis"]["disabled_over_raw_rps"], 1.0)
            with self.assertRaises(ValueError):
                runner.run(__file__, 8)

    def exercise(self, result):
        servers = []
        original = http.server.ThreadingHTTPServer

        def create(*args, **kwargs):
            server = original(*args, **kwargs)
            servers.append(server)
            return server

        try:
            with mock.patch.object(runner.http.server, "ThreadingHTTPServer", side_effect=create), \
                 mock.patch.object(runner.subprocess, "run", side_effect=result if isinstance(result, Exception) else None,
                                   return_value=result) as execute, \
                 contextlib.redirect_stderr(io.StringIO()):
                report = runner.run(__file__, 8)
                self.assertEqual(execute.call_count, 8)
                self.assertTrue(all(call.kwargs["timeout"] == 60 for call in execute.call_args_list))
                return report
        finally:
            self.assertEqual(len(servers), 1)
            self.assertEqual(servers[0].socket.fileno(), -1)
            self.assertTrue(servers[0]._BaseServer__is_shut_down.is_set())

    def test_complete_evidence(self):
        report = self.exercise(subprocess.CompletedProcess([], 0, complete_output(), ""))
        self.assertEqual(len(report["runs"]), 8)
        self.assertEqual(report["analysis"]["assessment"], "within_margin")
        self.assertEqual(len(report["executable_sha256"]), 64)

    def test_process_failure_closes_peer(self):
        with self.assertRaises(RuntimeError):
            self.exercise(subprocess.CompletedProcess([], 1, "", ""))

    def test_timeout_closes_peer(self):
        with self.assertRaises(subprocess.TimeoutExpired):
            self.exercise(subprocess.TimeoutExpired("benchmark", 60))

    def test_partial_evidence_closes_peer(self):
        with self.assertRaises(ValueError):
            self.exercise(subprocess.CompletedProcess([], 0, "", ""))

    def test_cli_propagates_assessment_and_preserves_evidence(self):
        for assessment, status in (("within_margin", 0), ("regression", 1), ("inconclusive", 2)):
            with self.subTest(assessment=assessment), tempfile.TemporaryDirectory() as directory:
                output = Path(directory) / "evidence.json"
                report = {"analysis": {"assessment": "within_margin", "combined_assessment": assessment}}
                with mock.patch.object(runner.sys, "argv", ["runner", __file__, "--output", str(output)]), \
                     mock.patch.object(runner, "run", return_value=report) as execute, \
                     contextlib.redirect_stdout(io.StringIO()), contextlib.redirect_stderr(io.StringIO()):
                    self.assertEqual(runner.main(), status)
                    before = output.read_bytes()
                    with self.assertRaises(SystemExit):
                        runner.main()
                    self.assertEqual(execute.call_count, 1)
                    self.assertEqual(output.read_bytes(), before)


if __name__ == "__main__":
    unittest.main()
