#!/usr/bin/env python3
"""Retain each round from unmodified HttpArena WS driver text logs."""
import argparse
import csv
import json
from pathlib import Path
import re


def microseconds(value):
    match = re.fullmatch(r"([\d.]+)(us|ms|s)", value)
    if not match:
        raise ValueError(f"unsupported latency: {value}")
    return float(match[1]) * {"us": 1, "ms": 1000, "s": 1000000}[match[2]]


def parse_log(path):
    text = path.read_text(encoding="utf-8")
    header = None
    round_number = None
    body = []
    rows = []

    def finish():
        if header is None or round_number is None:
            return
        raw = "\n".join(body)
        counts = re.search(r"(\d+) frames sent\s+in ([\d.]+)s, (\d+) frames received", raw)
        latency = re.search(r"Latency\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)", raw)
        stats = re.search(r"\[info\] CPU ([\d.]+)% \| Mem (\S+)", raw)
        if not (counts and latency and stats):
            raise ValueError(f"incomplete round {header} round={round_number}: {path}")
        sent, seconds, received = int(counts[1]), float(counts[2]), int(counts[3])
        if seconds <= 0 or received <= 0:
            raise ValueError(f"zero measured traffic: {path}")
        errors = re.search(r"Errors: connect (\d+), read (\d+), timeout (\d+)", raw)
        connect, read, timeout = map(int, errors.groups()) if errors else (0, 0, 0)
        reconnects = re.search(r"Reconnects: (\d+)", raw)
        refused = re.search(r"WS upgrades refused: (\d+)", raw)
        rows.append(dict(framework=header[0], profile=header[1], connections=header[2],
                         round=round_number, frames_sent=sent, frames_received=received,
                         elapsed_seconds=seconds, frames_per_second=int(received / seconds),
                         latency_us=dict(zip(("mean", "p50", "p90", "p99", "p999"),
                                             map(microseconds, latency.groups()))),
                         cpu_percent=float(stats[1]), memory=stats[2],
                         errors=dict(connect=connect, read=read, timeout=timeout),
                         ws_upgrade_failures=int(refused[1]) if refused else 0,
                         reconnects=int(reconnects[1]) if reconnects else 0,
                         send_receive_delta=sent-received, source_log=str(path), raw_round=raw))

    for line in text.splitlines():
        match = re.search(r"=== (\S+) / (echo-ws(?:-pipeline|-limited)?) / (\d+)c", line)
        if match:
            finish()
            header = (match[1], match[2], int(match[3]))
            round_number = None
            body = []
        match = re.search(r"\[run (\d+)/(\d+)\]", line)
        if match:
            finish()
            round_number = int(match[1])
            body = []
        if round_number is not None:
            body.append(line)
    finish()
    if not rows:
        raise ValueError(f"no completed official rounds: {path}")
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("results", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for path in sorted(args.results.glob("*/echo-ws*.log")):
        rows.extend(parse_log(path))
    if not rows:
        parser.error("no completed official logs")
    groups = {}
    for row in rows:
        groups.setdefault((row["framework"], row["profile"], row["connections"]), []).append(row)
    for key, samples in groups.items():
        if sorted(row["round"] for row in samples) != [1, 2, 3]:
            raise ValueError(f"expected all three rounds: {key}")
    expected = {(profile, connections)
                for profile, counts in {"echo-ws": (512, 4096, 16384),
                                        "echo-ws-pipeline": (512, 4096, 16384),
                                        "echo-ws-limited": (512, 4096)}.items()
                for connections in counts}
    for framework in {row["framework"] for row in rows}:
        actual = {(profile, connections) for name, profile, connections in groups
                  if name == framework}
        if actual != expected:
            raise ValueError(f"incomplete official configuration matrix: {framework}: {actual}")
    args.output.mkdir(parents=True, exist_ok=True)
    (args.output/"rounds.json").write_text(json.dumps(rows, indent=2)+"\n")
    summary = []
    for key, samples in sorted(groups.items()):
        best = max(samples, key=lambda sample: sample["frames_per_second"])
        summary.append(dict(framework=key[0], profile=key[1], connections=key[2],
                            best_frames_per_second=best["frames_per_second"],
                            best_round=best["round"], best_p99_us=best["latency_us"]["p99"],
                            best_cpu_percent=best["cpu_percent"], best_memory=best["memory"],
                            min_frames_per_second=min(row["frames_per_second"] for row in samples),
                            total_error_events=sum(sum(row["errors"].values()) + row["ws_upgrade_failures"] for row in samples)))
    (args.output/"summary.json").write_text(json.dumps(summary, indent=2)+"\n")
    with (args.output/"summary.csv").open("w", newline="") as stream:
        writer = csv.DictWriter(stream, fieldnames=list(summary[0]))
        writer.writeheader()
        writer.writerows(summary)
    for row in summary:
        print(f'{row["framework"]:24} {row["profile"]:18} {row["connections"]:5} '
              f'{row["best_frames_per_second"]:10,} frames/s p99={row["best_p99_us"]:9,.0f} us '
              f'errors(3 rounds)={row["total_error_events"]}')


if __name__ == "__main__":
    main()
