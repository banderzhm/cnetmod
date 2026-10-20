import unittest

from compare_ws_target import CASES, compare


def matrix(frames):
    return {key: [dict(round=i, frames_per_second=frames, latency_us={"p99": 5},
                       errors={"connect": 0, "read": 0, "timeout": 0}) for i in (1, 2, 3)]
            for key in CASES}


class TargetTests(unittest.TestCase):
    def test_ten_percent_is_strict_and_uses_fastest_peer_per_configuration(self):
        slow, fast = matrix(80), matrix(101)
        failed = compare(matrix(111), {"slow": slow, "fast": fast})
        self.assertFalse(failed["meets_throughput_target"])
        self.assertTrue(all(row["required_frames_per_second"] == 112 for row in failed["rows"]))
        self.assertTrue(compare(matrix(112), {"fast": fast})["meets_throughput_target"])

    def test_one_regressing_configuration_cannot_be_hidden_by_others(self):
        candidate = matrix(200)
        for row in candidate[sorted(CASES)[0]]:
            row["frames_per_second"] = 100
        self.assertFalse(compare(candidate, {"peer": matrix(100)})["meets_throughput_target"])

    def test_missing_configuration_is_not_an_implicit_success(self):
        incomplete = matrix(200)
        incomplete.pop(sorted(CASES)[0])
        with self.assertRaises(ValueError):
            compare(incomplete, {"peer": matrix(100)})


if __name__ == "__main__":
    unittest.main()
