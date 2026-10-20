#!/usr/bin/env python3
"""Retain each official run alongside the driver's saved best-of-three row."""

import argparse
import json
from pathlib import Path
import re


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--stage", type=Path, required=True)
    args = parser.parse_args()
    stage = args.stage
    images = json.loads((stage / "records/images.json").read_text())
    summary = {}
    for name, entry in images.items():
        for profile in entry["tests"]:
            text = (stage / f"records/{name}-{profile}.log").read_text()
            rounds = []
            for number, block in re.findall(r"\[run (\d+)/3\](.*?)(?=\[run \d+/3\]|=== Best:)", text, re.S):
                requests = re.search(r"requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout", block)
                statuses = re.search(r"status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx", block)
                duration = re.search(r"finished in ([\d.]+)s,", block)
                latency = re.search(r"^request\s+:\s+(.+)$", block, re.M)
                resources = re.search(r"\[info\] CPU (\S+) \| Mem (\S+)", block)
                if not all((requests, statuses, duration, latency)):
                    raise RuntimeError(f"Incomplete official output: {name}/{profile} run {number}")
                counts = dict(zip(("total", "started", "done", "succeeded", "failed", "errored", "timeout"), map(int, requests.groups())))
                status = dict(zip(("2xx", "3xx", "4xx", "5xx"), map(int, statuses.groups())))
                fields = latency.group(1).split()
                rounds.append({"round": int(number), "official_rps": int(status["2xx"] / float(duration.group(1))),
                               "completed_rps": int(counts["succeeded"] / float(duration.group(1))),
                               "elapsed_s": float(duration.group(1)), "requests": counts, "status_codes": status,
                               "success_fraction": counts["succeeded"] / counts["total"] if counts["total"] else 0,
                               "success_per_started": counts["succeeded"] / counts["started"] if counts["started"] else 0,
                               "unfinished_started": counts["started"] - counts["done"],
                               "headers_ahead_of_completions": status["2xx"] - counts["succeeded"],
                               "failure_fraction": counts["failed"] / counts["total"] if counts["total"] else 0,
                               "sampled_cpu": resources.group(1) if resources else None,
                               "peak_memory": resources.group(2) if resources else None,
                               "p50": fields[2], "p95": fields[3], "p99": fields[4], "mean": fields[5],
                               "valid": counts["succeeded"] > 0 and not any(counts[k] for k in ("failed", "errored", "timeout")) and not any(status[k] for k in ("3xx", "4xx", "5xx"))})
            if len(rounds) != 3:
                raise RuntimeError(f"Expected three official rounds: {name}/{profile}")
            selected_path = stage / f"results/{profile}/64/{name}.json"
            selected = json.loads(selected_path.read_text())
            expected = max(item["official_rps"] for item in rounds)
            assert selected["rps"] == expected, (selected["rps"], expected)
            summary[f"{name}/{profile}"] = {"rounds": rounds, "official_selected": selected,
                                           "valid": all(item["valid"] for item in rounds)}
    output = stage / "records/summary.json"
    output.write_text(json.dumps(summary, indent=2) + "\n")
    print(json.dumps({name: {"rps": item["official_selected"]["rps"], "p99": item["official_selected"]["p99_latency"], "valid": item["valid"]} for name, item in summary.items()}, indent=2))


if __name__ == "__main__":
    main()
