import tempfile
from pathlib import Path
import unittest

from grpc_arena_bench import cpus, parse_result


class HarnessTests(unittest.TestCase):
    def test_affinity(self):
        self.assertEqual(cpus("0-2,5,7-8"), {0, 1, 2, 5, 7, 8})
        for value in ["", "-1", "3-1"]:
            with self.assertRaises(ValueError):
                cpus(value)

    def test_latency_units_and_error_accounting(self):
        output = ("requests: 5 total, 5 started, 5 done, 3 succeeded, "
                  "2 failed, 1 errored, 0 timeout\n"
                  "status codes: 3 2xx, 0 3xx, 1 4xx, 0 5xx\n")
        with tempfile.TemporaryDirectory() as directory:
            log = Path(directory) / "requests.tsv"
            log.write_text("1\t200\t1000\n2\t-1\t4000\n3\t400\t8000\n"
                           "4\t200\t2000\n5\t200\t3000\n")
            parsed = parse_result(output, log, 5)
            self.assertEqual(parsed["rps_http_2xx"], .6)
            self.assertEqual(parsed["http_error_rate"], .25)
            self.assertEqual(parsed["request_failure_rate"], .4)
            self.assertEqual(parsed["network_error_rate"], .2)
            self.assertEqual(parsed["latency_samples"], 3)
            self.assertEqual(parsed["latency_ms"]["p99"], 3)
            self.assertEqual(parsed["latency_ms"]["mean"], 2)

    def test_missing_or_zero_results_are_not_success(self):
        with tempfile.TemporaryDirectory() as directory:
            with self.assertRaises(ValueError):
                parse_result("connection failed", Path(directory) / "missing", 5)
            output = ("requests: 1 total, 1 started, 0 done, 0 succeeded, "
                      "1 failed, 1 errored, 0 timeout\n"
                      "status codes: 0 2xx, 0 3xx, 0 4xx, 0 5xx\n")
            with self.assertRaises(ValueError):
                parse_result(output, Path(directory) / "missing", 5)


if __name__ == "__main__":
    unittest.main()
