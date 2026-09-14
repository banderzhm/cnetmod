import copy
import unittest

from analyze_http_observation import analyze, analyze_report


def fixture(ratio=1.0):
    return {"rows": [
        {"round": index, "mode": mode, "requests": 1000,
         "rps": 1000.0 * (ratio if mode == "disabled" else 1)}
        for index in range(8) for mode in ("raw", "disabled")
    ]}


class AnalysisTests(unittest.TestCase):
    def test_missing_latency_cannot_pass_combined_gate(self):
        report = analyze(fixture(), repetitions=1000)
        self.assertEqual(report["assessment"], "within_margin")
        self.assertEqual(report["combined_assessment"], "inconclusive")

    def test_tail_regression_overrides_throughput_gain(self):
        data = fixture(1.1)
        for row in data["rows"]:
            row.update(p50_us=10, p99_us=40 if row["mode"] == "disabled" else 20)
        report = analyze(data, repetitions=1000)
        self.assertEqual(report["assessment"], "within_margin")
        self.assertEqual(report["latency"]["p99_us"]["assessment"], "regression")
        self.assertEqual(report["combined_assessment"], "regression")
        historical = {"baseline_executable": "historical", "runs": [
            {"process": index, "rows": [row for row in data["rows"] if row["mode"] == "disabled"],
             "baseline_rows": [row for row in data["rows"] if row["mode"] == "raw"]} for index in range(8)]}
        report = analyze_report(historical, repetitions=1000)
        self.assertAlmostEqual(report["latency"]["p99_us"]["disabled_over_raw"], 2)
        self.assertEqual(report["combined_assessment"], "regression")

    def test_invalid_latency_rejected(self):
        for value in (None, True, 0, -1, float("nan"), float("inf"), 30):
            data = fixture()
            for row in data["rows"]:
                row.update(p50_us=10, p99_us=20)
            data["rows"][0]["p50_us"] = value
            with self.subTest(value=value), self.assertRaises(ValueError):
                analyze(data, repetitions=1000)

    def test_replays_process_report_without_trusting_assessment(self):
        data = {"runs": [{"process": index, "rows": fixture(0.9)["rows"]}
                         for index in range(8)],
                "analysis": {"assessment": "within_margin"}}
        report = analyze_report(data, repetitions=1000)
        self.assertEqual(report["assessment"], "regression")
        self.assertEqual(report["paired_rounds"], 8)

    def test_replays_historical_disabled_only_report(self):
        rows = fixture(0.9)["rows"]
        data = {"baseline_executable": "historical",
                "runs": [{"process": index,
                          "rows": [row for row in rows if row["mode"] == "disabled"],
                          "baseline_rows": [row for row in rows if row["mode"] == "raw"]}
                         for index in range(8)]}
        self.assertEqual(analyze_report(data, repetitions=1000)["assessment"], "regression")
        for change in ("duplicate", "missing", "counts"):
            invalid = copy.deepcopy(data)
            if change == "duplicate":
                invalid["runs"][1]["process"] = 0
            elif change == "missing":
                invalid["runs"][0]["baseline_rows"].pop()
            else:
                invalid["runs"][0]["rows"][0]["requests"] = 999
            with self.subTest(change=change), self.assertRaises(ValueError):
                analyze_report(invalid, repetitions=1000)

    def test_equal(self):
        self.assertEqual(analyze(fixture(), repetitions=1000)["assessment"], "within_margin")

    def test_regression_and_explicit_margin(self):
        self.assertEqual(analyze(fixture(0.9), repetitions=1000)["assessment"], "regression")
        self.assertEqual(analyze(fixture(0.99), tolerance=0.02, repetitions=1000)["assessment"], "within_margin")

    def test_inconclusive(self):
        data = fixture()
        for row in data["rows"]:
            if row["mode"] == "disabled":
                row["rps"] *= 0.9 if row["round"] % 2 else 1.1
        self.assertEqual(analyze(data, repetitions=1000)["assessment"], "inconclusive")

    def test_invalid_evidence(self):
        variants = []
        data = fixture()
        data["rows"].pop()
        variants.append(data)
        data = fixture()
        data["rows"].append(copy.deepcopy(data["rows"][0]))
        variants.append(data)
        for field, value in (("rps", float("nan")), ("rps", 0), ("requests", 1)):
            data = fixture()
            data["rows"][0][field] = value
            variants.append(data)
        for data in variants:
            with self.subTest(data=data), self.assertRaises(ValueError):
                analyze(data, repetitions=1000)


if __name__ == "__main__":
    unittest.main()
