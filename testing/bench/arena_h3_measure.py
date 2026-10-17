#!/usr/bin/env python3
"""Sequential Linux h2load HTTP/3 sampling with raw output and process CPU.

Start and pin the server separately. This script never starts/stops other jobs.
CPU includes h2load's explicit warmup; throughput/latencies exclude that warmup.
"""
import argparse
from collections import Counter
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import subprocess
import time


def cpu_seconds(pid):
    # comm is parenthesized and may contain spaces or parentheses.
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def parse_output(output, measured_seconds=None):
    finished = re.search(r"finished in ([\d.]+)s,", output)
    counts = re.search(
        r"requests: (\d+) total, (\d+) started, (\d+) done, (\d+) succeeded, "
        r"(\d+) failed, (\d+) errored, (\d+) timeout", output)
    statuses = re.search(r"status codes: (\d+) 2xx, (\d+) 3xx, (\d+) 4xx, (\d+) 5xx", output)
    if not finished or not counts or not statuses:
        raise ValueError("missing h2load measurement (not a successful zero result)")
    names = ("total", "started", "done", "succeeded", "failed", "errored", "timeout")
    result = dict(zip(names, map(int, counts.groups())))
    result["status_codes"] = dict(zip(("2xx", "3xx", "4xx", "5xx"), map(int, statuses.groups())))
    result["reported_wall_seconds"] = float(finished[1])
    result["seconds"] = measured_seconds if measured_seconds is not None else float(finished[1])
    result["rps_2xx"] = int(statuses[1]) / result["seconds"]
    # h2load's failed count already includes 4xx/5xx and errored/timeouts.
    # Redirects count as succeeded in h2load but fail this 2xx workload.
    result["error_rate"] = (max(result["failed"], int(statuses[3]) + int(statuses[4])) + int(statuses[2])) / max(1, result["total"])
    result["rps_completed"] = result["succeeded"] / result["seconds"]
    # h2load gates headers and stream-close counters independently on the
    # warmup phase. A stream crossing that boundary may have a completion
    # but no measured status. Preserve the discrepancy rather than inventing
    # an HTTP error or silently treating it as a 2xx response.
    result["status_counter_delta"] = result["done"] - sum(map(int, statuses.groups()))
    result["valid"] = int(statuses[1]) > 0 and result["error_rate"] == 0
    return result


def latency_summary(path):
    histogram = Counter()
    with path.open() as rows:
        for row in rows:
            fields = row.split()
            if len(fields) >= 3 and fields[1].isdigit() and 200 <= int(fields[1]) < 300:
                histogram[int(fields[2])] += 1
    samples = sum(histogram.values())
    if not samples:
        raise ValueError("no successful request latencies")
    result = {"samples": samples, "mean_us": sum(v * count for v, count in histogram.items()) / samples}
    cumulative = 0
    for value, count in sorted(histogram.items()):
        cumulative += count
        for p in (50, 95, 99):
            if cumulative >= math.ceil(samples * p / 100):
                result.setdefault(f"p{p}_us", value)
    return result


def latency_from_output(output):
    # nghttp2 v1.69.0: min, max, median, p95, p99, mean, sd, +/-sd.
    line = re.search(r"^\s*request\s*:\s*(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)\s+(\S+)", output, re.MULTILINE)
    if not line:
        raise ValueError("h2load v1.69.0 request percentile table required")
    def microseconds(value):
        match = re.fullmatch(r"([\d.]+)(ns|us|ms|s)", value)
        if not match:
            raise ValueError(f"invalid h2load duration: {value}")
        return float(match[1]) * {"ns": 0.001, "us": 1, "ms": 1000, "s": 1000000}[match[2]]
    return {"source": "h2load reservoir samples", **dict(zip(
        ("minimum_us", "maximum_us", "p50_us", "p95_us", "p99_us", "mean_us"),
        map(microseconds, line.groups())))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--pid", type=int, required=True)
    parser.add_argument("--loadgen", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--url", default="https://127.0.0.1:8443/baseline2?a=1&b=1")
    parser.add_argument("--client-cpus", default="16-31")
    parser.add_argument("--connections", type=int, default=64)
    parser.add_argument("--streams", type=int, default=64)
    parser.add_argument("--threads", type=int, default=16)
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--runs", type=int, default=3)
    parser.add_argument("--request-log", action="store_true",
                        help="optional full per-request TSV; adds I/O overhead")
    args = parser.parse_args()
    if min(args.connections, args.streams, args.threads, args.duration, args.runs) < 1 or args.warmup < 0:
        parser.error("positive counts/duration and nonnegative warmup required")
    args.output.mkdir(parents=True, exist_ok=False)
    metadata = {"platform": platform.platform(), "pid": args.pid,
                "server_affinity": sorted(os.sched_getaffinity(args.pid)),
                "server_command": Path(f"/proc/{args.pid}/cmdline").read_bytes().replace(b"\0", b" ").decode(),
                "loadgen_version": subprocess.check_output([args.loadgen, "--version"], text=True).strip(),
                "cpu_info": Path("/proc/cpuinfo").read_text(), "configuration": {**vars(args), "output": str(args.output)}}
    (args.output / "environment.json").write_text(json.dumps(metadata, indent=2))
    results = []
    for index in range(1, args.runs + 1):
        log = args.output / f"run{index}.tsv"
        command = ["taskset", "-c", args.client_cpus, args.loadgen, "--alpn-list=h3",
                   "--tls13-ciphers=TLS_AES_128_GCM_SHA256", "--groups=X25519",
                   args.url, "-c", str(args.connections), "-m", str(args.streams),
                   "-t", str(args.threads), "-D", f"{args.duration}s",
                   f"--warm-up-time={args.warmup}s"]
        if args.request_log:
            command.append(f"--log-file={log}")
        (args.output / f"run{index}.command.txt").write_text(shlex.join(command) + "\n")
        before_cpu, before_wall = cpu_seconds(args.pid), time.monotonic()
        completed = subprocess.run(command, text=True, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, timeout=args.duration + args.warmup + 40)
        wall, cpu = time.monotonic() - before_wall, cpu_seconds(args.pid) - before_cpu
        (args.output / f"run{index}.txt").write_text(completed.stdout)
        result = {"run": index, "returncode": completed.returncode, "server_cpu_seconds": cpu,
                  "wall_seconds_including_warmup": wall, "server_cpu_percent_including_warmup": cpu / wall * 100,
                  **parse_output(completed.stdout, args.duration),
                  "latency": latency_summary(log) if args.request_log else latency_from_output(completed.stdout)}
        result["valid"] &= completed.returncode == 0
        results.append(result)
        (args.output / "summary.json").write_text(json.dumps(results, indent=2))
        print(json.dumps(result), flush=True)
        if not result["valid"]:
            return 1
        time.sleep(2)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
