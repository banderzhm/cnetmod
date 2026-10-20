import tempfile
from pathlib import Path
import unittest

from analyze_official_grpc import parse_log


class OfficialGrpcAnalysisTests(unittest.TestCase):
    def parse(self, completed=1000000, failed=0, omit_latency=False):
        text = "=== fixture / unary-grpc / 256c (tool=h2load) ===\n[run 1/3]\n"
        text += f"finished in 5.12s, 1 req/s, 1MB/s\nrequests: {completed} total, "
        text += f"{completed + 25600} started, {completed} done, {completed} succeeded, "
        text += f"{failed} failed, 0 errored, 0 timeout\n"
        text += f"status codes: {completed} 2xx, 0 3xx, 0 4xx, 0 5xx\n"
        if not omit_latency:
            text += "time for request: 1ms 20ms 5ms 2ms 90%\n"
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder) / "round.log"
            path.write_text(text)
            return parse_log(path)[0]

    def test_uses_actual_duration_and_labels_max_latency(self):
        row = self.parse()
        self.assertEqual(row["rps"], 195312)
        self.assertEqual(row["max_latency"], "20ms")
        self.assertTrue(row["valid"])
        self.assertTrue(row["progressing_beyond_initial_batches"])

    def test_errors_and_one_batch_stall_cannot_pass(self):
        self.assertFalse(self.parse(failed=1)["valid"])
        self.assertFalse(self.parse(completed=25600)["progressing_beyond_initial_batches"])

    def test_incomplete_round_is_not_silently_dropped(self):
        with self.assertRaises(ValueError):
            self.parse(omit_latency=True)


if __name__ == "__main__":
    unittest.main()
