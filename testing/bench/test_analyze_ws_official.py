import tempfile
import subprocess
import sys
from pathlib import Path
import unittest

from analyze_ws_official import parse_log


class OfficialLogTests(unittest.TestCase):
    def parse(self, text):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory)/"official.log"
            path.write_text(text)
            return parse_log(path)

    def sample(self, round_number, extra=""):
        return f"""[run {round_number}/3]
  Thread Stats   Avg      p50      p90      p99    p99.9
    Latency    166us     68us    219us   2.17ms   6.77ms
  1003 frames sent in 5.00s, 1000 frames received
  Reconnects: 3
{extra}
[info] CPU 1520.1% | Mem 64MiB
"""

    def test_keeps_all_rounds_and_correct_received_frame_numerator(self):
        raw = "=== engine / echo-ws / 16384c (tool=gcannon) ===\n"
        raw += "".join(self.sample(i) for i in (1, 2, 3))
        rows = self.parse(raw)
        self.assertEqual([row["round"] for row in rows], [1, 2, 3])
        self.assertEqual(rows[0]["frames_per_second"], 200)
        self.assertEqual(rows[0]["send_receive_delta"], 3)
        self.assertEqual(rows[0]["latency_us"]["p99"], 2170)

    def test_reconnects_are_distinct_from_transport_errors(self):
        raw = "=== engine / echo-ws-limited / 512c (tool=gcannon) ===\n"
        rows = self.parse(raw+self.sample(1, "Errors: connect 1, read 2, timeout 3"))
        self.assertEqual(rows[0]["reconnects"], 3)
        self.assertEqual(rows[0]["errors"], dict(connect=1, read=2, timeout=3))
        self.assertEqual(sum(rows[0]["errors"].values()), 6)

    def test_incomplete_or_zero_traffic_round_is_rejected(self):
        header = "=== engine / echo-ws-pipeline / 512c (tool=gcannon) ===\n"
        with self.assertRaises(ValueError):
            self.parse(header+"[run 1/3]\n")
        with self.assertRaises(ValueError):
            self.parse(header+self.sample(1).replace("1000 frames received", "0 frames received"))

    def test_upgrade_refusals_are_kept_separately(self):
        raw = "=== engine / echo-ws-limited / 512c (tool=gcannon) ===\n"
        row = self.parse(raw+self.sample(1, "WS upgrades refused: 2"))[0]
        self.assertEqual(row["ws_upgrade_failures"], 2)

    def test_complete_rounds_cannot_hide_missing_official_configurations(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            entry = root/"engine"
            entry.mkdir()
            raw = "=== engine / echo-ws / 512c (tool=gcannon) ===\n"
            raw += "".join(self.sample(i) for i in (1, 2, 3))
            (entry/"echo-ws.log").write_text(raw)
            result = subprocess.run([sys.executable,
                                     str(Path(__file__).with_name("analyze_ws_official.py")),
                                     str(root), "--output", str(root/"analysis")],
                                    capture_output=True, text=True)
            self.assertNotEqual(result.returncode, 0)
            self.assertIn("incomplete official configuration matrix", result.stderr)
            self.assertFalse((root/"analysis"/"summary.json").exists())


if __name__ == "__main__":
    unittest.main()
