#!/usr/bin/env python3
"""Keep every official gRPC round and reject errors, stalls and missing cases."""
import argparse
import json
import math
from pathlib import Path
import re

CASES = [(profile, count) for profile in ("unary-grpc", "unary-grpc-tls")
         for count in (256, 1024)]


def parse_log(path):
    rows = []
    current = None
    text = []

    def finish():
        if current is None:
            return
        block = "\n".join(text)
        duration = re.search(r"finished in ([\d.]+)s,", block)
        requests = re.search(r"requests: (\d+) total, (\d+) started, (\d+) done, "
                             r"(\d+) succeeded, (\d+) failed, (\d+) errored, (\d+) timeout", block)
        statuses = re.search(r"status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx", block)
        latency = re.search(r"time for request:\s+(\S+)\s+(\S+)\s+(\S+)", block)
        if not all((duration, requests, statuses, latency)):
            raise ValueError(f"Incomplete round {path}: {current}")
        total, started, done, succeeded, failed, errored, timeout = map(int, requests.groups())
        ok, redirects, client_errors, server_errors = map(int, statuses.groups())
        seconds = float(duration[1])
        valid = (seconds > 0 and done == succeeded == ok and
                 failed == errored == timeout == redirects == client_errors == server_errors == 0)
        progressing = done > int(current["connections"]) * 100 * 2
        resources = re.search(r"\[info\] CPU ([^|]+)\| Mem (\S+)", block)
        rows.append(dict(current, duration_seconds=seconds, rps=int(ok / seconds),
                         total=total, started=started, completed=done, succeeded=succeeded,
                         failed=failed, errored=errored, timeout=timeout,
                         valid=valid, progressing_beyond_initial_batches=progressing,
                         min_latency=latency[1], max_latency=latency[2], mean_latency=latency[3],
                         cpu=resources[1].strip() if resources else None,
                         memory=resources[2] if resources else None, source=str(path)))

    entry = profile = connections = None
    for line in path.read_text().splitlines():
        heading = re.search(r"=== (\S+) / (unary-grpc(?:-tls)?) / (\d+)c", line)
        run = re.search(r"\[run (\d+)/3\]", line)
        if heading:
            finish()
            current = None
            entry, profile, connections = heading.groups()
        elif run:
            finish()
            current = dict(entry=entry, profile=profile, connections=int(connections), round=int(run[1]))
            text = []
        elif current is not None:
            text.append(line)
    finish()
    return rows


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--records", type=Path, nargs="+", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--reference", action="append", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    rows = []
    for directory in args.records:
        for path in sorted(directory.glob("*-unary-grpc*.log")):
            rows.extend(parse_log(path))
    comparison = []
    for profile, connections in CASES:
        entries = {}
        for entry in [args.candidate, *args.reference]:
            selected = [r for r in rows if (r["entry"], r["profile"], r["connections"]) ==
                        (entry, profile, connections)]
            if {r["round"] for r in selected} != {1, 2, 3} or len(selected) != 3:
                raise ValueError(f"Need exactly three rounds: {entry} {profile} {connections}")
            if not all(r["valid"] and r["progressing_beyond_initial_batches"] for r in selected):
                raise ValueError(f"Errors or stalled connection: {entry} {profile} {connections}")
            entries[entry] = max(r["rps"] for r in selected)
        fastest = max(entries[e] for e in args.reference)
        candidate = entries[args.candidate]
        comparison.append(dict(profile=profile, connections=connections, rps=entries,
                               target=math.ceil(fastest * 1.1),
                               gain_percent=(candidate / fastest - 1) * 100,
                               passed=candidate >= math.ceil(fastest * 1.1)))
    result = dict(headline="Official best of three successful-2xx RPS",
                  latency_note="Official p99_latency is maximum; no true percentile claimed",
                  rounds=rows, comparison=comparison,
                  all_passed=all(c["passed"] for c in comparison))
    args.output.write_text(json.dumps(result, indent=2) + "\n")
    print(json.dumps(comparison, indent=2))
    return 0 if result["all_passed"] else 1


if __name__ == "__main__":
    raise SystemExit(main())
