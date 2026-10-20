#!/usr/bin/env python3
"""Check every official WS configuration against the fastest measured peer."""
import argparse
import json
from pathlib import Path

from analyze_ws_official import parse_log

CASES = {(profile, connections)
         for profile, counts in {"echo-ws": (512, 4096, 16384),
                                 "echo-ws-pipeline": (512, 4096, 16384),
                                 "echo-ws-limited": (512, 4096)}.items()
         for connections in counts}


def load_entry(directory):
    groups = {}
    for path in sorted(directory.glob("echo-ws*.log")):
        for row in parse_log(path):
            groups.setdefault((row["profile"], row["connections"]), []).append(row)
    if set(groups) != CASES:
        raise ValueError(f"missing official configurations: {directory}")
    for key, rows in groups.items():
        if sorted(row["round"] for row in rows) != [1, 2, 3]:
            raise ValueError(f"expected exactly three rounds: {directory}: {key}")
    return groups


def compare(candidate, peers, margin_percent=10):
    if margin_percent < 0:
        raise ValueError("margin must be nonnegative")
    if not peers or set(candidate) != CASES or any(set(peer) != CASES for peer in peers.values()):
        raise ValueError("complete candidate and peer matrices are required")
    results = []
    for key in sorted(CASES):
        best = max(candidate[key], key=lambda row: row["frames_per_second"])
        name, reference = max(((name, max(peer[key], key=lambda row: row["frames_per_second"]))
                               for name, peer in peers.items()),
                              key=lambda pair: pair[1]["frames_per_second"])
        observed, peer_fps = best["frames_per_second"], reference["frames_per_second"]
        required = (peer_fps * (100 + margin_percent) + 99) // 100
        results.append(dict(profile=key[0], connections=key[1], candidate_frames_per_second=observed,
                            reference_peer=name, reference_frames_per_second=peer_fps,
                            required_frames_per_second=required, meets_target=observed >= required,
                            improvement_percent=(observed / peer_fps - 1) * 100,
                            candidate_p99_us=best["latency_us"]["p99"],
                            reference_p99_us=reference["latency_us"]["p99"],
                            candidate_error_events=sum(sum(row["errors"].values()) for row in candidate[key]),
                            reference_error_events=sum(sum(row["errors"].values()) for row in peers[name][key])))
    return dict(margin_percent=margin_percent, meets_throughput_target=all(row["meets_target"] for row in results),
                criterion="Each of all eight configurations must beat the fastest peer's official best of three; correctness gates are separate.",
                rows=results)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("candidate", type=Path)
    parser.add_argument("--peer", type=Path, action="append", required=True)
    parser.add_argument("--margin-percent", type=int, default=10)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    result = compare(load_entry(args.candidate),
                     {path.name: load_entry(path) for path in args.peer}, args.margin_percent)
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2)+"\n", encoding="utf-8")
    for row in result["rows"]:
        print(f'{row["profile"]:18} {row["connections"]:5} {row["candidate_frames_per_second"]:>10,} '
              f'vs {row["reference_peer"]:16} {row["reference_frames_per_second"]:>10,} '
              f'{row["improvement_percent"]:+7.2f}% {"PASS" if row["meets_target"] else "FAIL"}')
    return 0 if result["meets_throughput_target"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
