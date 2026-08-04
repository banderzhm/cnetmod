#!/usr/bin/env python3
"""Aggregate reproducible oha JSON runs into CSV and Markdown tables."""

from __future__ import annotations

import argparse
import csv
import json
import re
import statistics
from collections import defaultdict
from pathlib import Path


RUN_FILE = re.compile(r"^(?P<scenario>.+)-run(?P<run>[0-9]+)\.json$")


def protocol_for(scenario: str) -> str:
    if "-h3" in scenario:
        return "HTTP/3"
    if scenario.endswith("-h2-tls"):
        return "HTTPS/2"
    if scenario.endswith("-h2c"):
        return "HTTP/2"
    if scenario.endswith("-h1-tls"):
        return "HTTPS/1.1"
    return "HTTP/1.1"


def mean(values: list[float]) -> float:
    return statistics.fmean(values) if values else 0.0


def main() -> None:
    parser = argparse.ArgumentParser()
    parser.add_argument("result_dir", type=Path)
    args = parser.parse_args()
    raw = args.result_dir / "raw"
    grouped: dict[str, list[dict]] = defaultdict(list)
    failed: set[str] = set()

    for path in sorted(raw.glob("*-run*.json")):
        match = RUN_FILE.match(path.name)
        if match is None:
            continue
        scenario = match.group("scenario")
        try:
            grouped[scenario].append(json.loads(path.read_text(encoding="utf-8")))
        except (OSError, json.JSONDecodeError):
            failed.add(scenario)

    rows = []
    for scenario in sorted(grouped.keys() | failed):
        runs = grouped.get(scenario, [])
        rps = [float(run["summary"]["requestsPerSec"]) for run in runs]
        success = [float(run["summary"]["successRate"]) for run in runs]
        p50 = [float(run["latencyPercentiles"]["p50"]) * 1000.0 for run in runs]
        p99 = [float(run["latencyPercentiles"]["p99"]) * 1000.0 for run in runs]
        rows.append(
            {
                "protocol": protocol_for(scenario),
                "scenario": scenario,
                "runs": len(runs),
                "rps_mean": mean(rps),
                "rps_min": min(rps, default=0.0),
                "rps_max": max(rps, default=0.0),
                "p50_ms": mean(p50),
                "p99_ms": mean(p99),
                "success_rate": mean(success),
                "status": (
                    "failed"
                    if scenario in failed or not runs
                    else "partial"
                    if mean(success) < 0.999999
                    else "passed"
                ),
            }
        )

    fields = list(rows[0]) if rows else []
    with (args.result_dir / "summary.csv").open("w", newline="", encoding="utf-8") as output:
        writer = csv.DictWriter(output, fieldnames=fields)
        writer.writeheader()
        writer.writerows(rows)

    lines = [
        "| Protocol | Framework | Runs | Mean req/s | Range req/s | P50 ms | P99 ms | Success | Status |",
        "|---|---|---:|---:|---:|---:|---:|---:|---|",
    ]
    for row in rows:
        lines.append(
            f"| {row['protocol']} | {row['scenario']} | {row['runs']} | "
            f"{row['rps_mean']:,.0f} | {row['rps_min']:,.0f}-{row['rps_max']:,.0f} | "
            f"{row['p50_ms']:.3f} | {row['p99_ms']:.3f} | "
            f"{row['success_rate'] * 100.0:.2f}% | {row['status']} |"
        )
    (args.result_dir / "summary.md").write_text("\n".join(lines) + "\n", encoding="utf-8")


if __name__ == "__main__":
    main()
