"""Analyze paired round throughput; never infer universal zero overhead."""

import argparse
import json
import math
import random
import statistics


def latency_analysis(pairs, tolerance, repetitions):
    """Compare reported round quantiles, not pooled request quantiles."""
    rows = [row for pair in pairs.values() for row in pair.values()]
    fields = ("p50_us", "p99_us")
    if not any(field in row for row in rows for field in fields):
        return {"assessment": "inconclusive", "reason": "latency evidence missing"}
    for row in rows:
        for field in fields:
            value = row.get(field)
            if isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value <= 0:
                raise ValueError("latency quantiles must be finite and positive")
        if row["p50_us"] > row["p99_us"]:
            raise ValueError("p50 exceeds p99")
    result = {}
    for field in fields:
        ratios = [math.log(pair["disabled"][field] / pair["raw"][field]) for pair in pairs.values()]
        rng = random.Random(0)
        estimates = sorted(math.exp(statistics.fmean(rng.choices(ratios, k=len(ratios))))
                           for _ in range(repetitions))
        lower = estimates[int(0.025 * (repetitions - 1))]
        upper = estimates[int(0.975 * (repetitions - 1))]
        threshold = 1 + tolerance
        result[field] = {
            "disabled_over_raw": math.exp(statistics.fmean(ratios)),
            "bootstrap_95_percent_interval": [lower, upper],
            "assessment": "within_margin" if upper <= threshold else "regression" if lower > threshold else "inconclusive",
        }
    result["assessment"] = combine_assessments([result[field]["assessment"] for field in fields])
    result["allowed_latency_increase"] = tolerance
    result["scope"] = "Geometric means of reported quantiles; not pooled latency percentiles. Intervals are per metric, not a joint 95% confidence region."
    return result


def combine_assessments(assessments):
    if "regression" in assessments:
        return "regression"
    return "within_margin" if all(item == "within_margin" for item in assessments) else "inconclusive"


def analyze(document, *, tolerance=0.0, repetitions=20000):
    if not math.isfinite(tolerance) or not 0 <= tolerance < 1:
        raise ValueError("tolerance must be finite and in [0, 1)")
    if repetitions < 1000:
        raise ValueError("at least 1000 bootstrap repetitions are required")
    pairs = {}
    for row in document["rows"]:
        mode = row["mode"]
        round_id = row["round"]
        if mode not in ("raw", "disabled") or type(round_id) is not int:
            raise ValueError("invalid round or mode")
        rps = row["rps"]
        count = row["requests"]
        if type(count) is not int or count <= 0:
            raise ValueError("requests must be a positive integer")
        if isinstance(rps, bool) or not isinstance(rps, (int, float)) or not math.isfinite(rps) or rps <= 0:
            raise ValueError("throughput must be finite and positive")
        pair = pairs.setdefault(round_id, {})
        if mode in pair:
            raise ValueError("duplicate mode within a round")
        pair[mode] = row
    if len(pairs) < 8:
        raise ValueError("at least eight complete paired rounds are required")
    ratios = []
    for pair in pairs.values():
        if set(pair) != {"raw", "disabled"}:
            raise ValueError("incomplete paired round")
        if pair["raw"]["requests"] != pair["disabled"]["requests"]:
            raise ValueError("paired request counts differ")
        ratios.append(math.log(pair["disabled"]["rps"] / pair["raw"]["rps"]))
    rng = random.Random(0)
    estimates = sorted(
        math.exp(statistics.fmean(rng.choices(ratios, k=len(ratios))))
        for _ in range(repetitions)
    )
    lower = estimates[int(0.025 * (repetitions - 1))]
    upper = estimates[int(0.975 * (repetitions - 1))]
    threshold = 1 - tolerance
    report = {
        "paired_rounds": len(ratios),
        "disabled_over_raw_rps": math.exp(statistics.fmean(ratios)),
        "paired_bootstrap_95_percent_interval": [lower, upper],
        "allowed_throughput_regression": tolerance,
        "assessment": "within_margin" if lower >= threshold else "regression" if upper < threshold else "inconclusive",
        "scope": "Current-build paired throughput only; assumes representative independent rounds. Not a pre-instrumentation baseline or latency equivalence claim.",
    }
    report["latency"] = latency_analysis(pairs, tolerance, repetitions)
    report["combined_assessment"] = combine_assessments([report["assessment"], report["latency"]["assessment"]])
    return report


def analyze_report(document, *, tolerance=0.0, repetitions=20000):
    """Recompute process-level evidence without trusting a stored assessment."""
    if "runs" not in document:
        return analyze(document, tolerance=tolerance, repetitions=repetitions)
    historical = document.get("baseline_executable") is not None
    aggregate = []
    seen = set()
    for run in document["runs"]:
        process = run["process"]
        if type(process) is not int or process < 0 or process in seen:
            raise ValueError("invalid or duplicate process identifier")
        seen.add(process)
        candidate = run["rows"]
        baseline = run.get("baseline_rows")
        if historical:
            if baseline is None or len(baseline) != 8 or any(row["mode"] != "raw" for row in baseline):
                raise ValueError("historical process requires eight raw baseline rounds")
            analyze({"rows": baseline + [dict(row, mode="disabled") for row in baseline]}, repetitions=1000)
            if len(candidate) == 8 and all(row["mode"] == "disabled" for row in candidate):
                validation = candidate + [dict(row, mode="raw") for row in candidate]
            else:
                validation = candidate
        else:
            if baseline is not None:
                raise ValueError("baseline rows without historical provenance")
            validation = candidate
        if len(validation) != 16:
            raise ValueError("process requires exactly eight paired rounds")
        analyze({"rows": validation}, repetitions=1000)
        if historical:
            raw_counts = {row["round"]: row["requests"] for row in baseline}
            disabled_counts = {row["round"]: row["requests"] for row in candidate if row["mode"] == "disabled"}
            if raw_counts != disabled_counts:
                raise ValueError("baseline and candidate round request counts differ")
        for mode in ("raw", "disabled"):
            source = baseline if historical and mode == "raw" else candidate
            selected = [row for row in source if row["mode"] == mode]
            summary = {"round": process, "mode": mode,
                              "requests": sum(row["requests"] for row in selected),
                              "rps": math.exp(statistics.fmean(math.log(row["rps"]) for row in selected))}
            if any("p50_us" in row or "p99_us" in row for row in selected):
                for field in ("p50_us", "p99_us"):
                    summary[field] = math.exp(statistics.fmean(math.log(row[field]) for row in selected))
            aggregate.append(summary)
    report = analyze({"rows": aggregate}, tolerance=tolerance, repetitions=repetitions)
    report["scope"] = (
        "Candidate disabled / supplied baseline raw throughput; build provenance must be verified separately. "
        if historical else "Current-build raw/disabled throughput. "
    ) + "Bootstrap unit is a process pair, not an individual round. Latency compares process geometric means of round quantiles; no universal equivalence claim."
    return report


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results")
    parser.add_argument("--tolerance", type=float, default=0.0)
    args = parser.parse_args()
    try:
        with open(args.results, encoding="utf-8") as source:
            report = analyze_report(json.load(source), tolerance=args.tolerance)
    except (ValueError, KeyError, TypeError, OSError) as error:
        parser.error(str(error))
    print(json.dumps(report, indent=2))
    return {"within_margin": 0, "regression": 1, "inconclusive": 2}[report["combined_assessment"]]


if __name__ == "__main__":
    raise SystemExit(main())
