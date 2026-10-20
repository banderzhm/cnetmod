"""Measurement regressions: never accept errors or parse failures as fast runs."""
import unittest
from arena_h3_measure import latency_from_output, parse_output


class MeasurementTests(unittest.TestCase):
    def output(self, failed=0, ok=100, status_404=0):
        return (f"finished in 5.00s, 20.00 req/s, 1KB/s\n"
                f"requests: 100 total, 100 started, {100-failed} done, {100-failed} succeeded, "
                f"{failed} failed, {failed} errored, 0 timeout\n"
                f"status codes: {ok} 2xx, 0 3xx, {status_404} 4xx, 0 5xx\n")

    def test_success_uses_2xx_rate(self):
        result = parse_output(self.output())
        self.assertTrue(result["valid"])
        self.assertEqual(result["rps_2xx"], 20)

    def test_transport_errors_are_not_double_counted(self):
        result = parse_output(self.output(failed=5, ok=95))
        self.assertFalse(result["valid"])
        self.assertEqual(result["error_rate"], 0.05)

    def test_404_is_not_a_successful_workload(self):
        result = parse_output(self.output(ok=90, status_404=10))
        self.assertFalse(result["valid"])
        self.assertEqual(result["rps_2xx"], 18)

    def test_http_errors_already_in_failed_are_not_double_counted(self):
        result = parse_output(self.output(failed=10, ok=90, status_404=10))
        self.assertEqual(result["error_rate"], 0.1)

    def test_missing_output_is_failure(self):
        with self.assertRaises(ValueError):
            parse_output("connection refused")

    def test_warmup_wall_time_does_not_dilute_measured_rps(self):
        output = self.output().replace("5.00s", "7.00s")
        result = parse_output(output, measured_seconds=5)
        self.assertEqual(result["rps_2xx"], 20)
        self.assertEqual(result["reported_wall_seconds"], 7)

    def test_warmup_counter_boundary_is_exposed(self):
        result = parse_output(self.output(ok=99))
        self.assertTrue(result["valid"])
        self.assertEqual(result["status_counter_delta"], 1)

    def test_h2load169_latency_units(self):
        latency = latency_from_output("request : 500us 1.2s 2.1ms 4.2ms 9.3ms 2.4ms 1ms 70%\n")
        self.assertEqual(latency["p99_us"], 9300)
        self.assertEqual(latency["p50_us"], 2100)


if __name__ == "__main__":
    unittest.main()
