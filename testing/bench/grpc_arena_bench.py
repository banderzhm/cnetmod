#!/usr/bin/env python3
"""Linux h2load harness. Run only while holding the shared performance token.

The server is an external process; use the same binary arguments, affinity and
load-generator build for baseline and candidate. HTTP success is not gRPC
success: run grpc_arena_check.py before/after each benchmark configuration.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
import os
from pathlib import Path
import platform
import re
import shlex
import statistics
import subprocess
import sys
import time
from urllib.parse import urlsplit


def cpus(value: str) -> set[int]:
    result = set()
    for part in value.split(","):
        lo, sep, hi = part.partition("-")
        first, last = int(lo), int(hi) if sep else int(lo)
        if first < 0 or last < first:
            raise ValueError(f"invalid CPU range: {part}")
        result.update(range(first, last + 1))
    if not result:
        raise ValueError("empty affinity")
    return result


def cpu_seconds(pid: int) -> float:
    # The comm field may contain spaces and parentheses. All threads are
    # included by the process stat counters, including worker pool threads.
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def percentile(ordered: list[int], p: float) -> float:
    return ordered[max(0, math.ceil(len(ordered) * p) - 1)] / 1000


def parse_result(output: str, log: Path, duration: float) -> dict:
    counts = re.search(
        r"requests:\s+(\d+) total,\s+(\d+) started,\s+(\d+) done,\s+"
        r"(\d+) succeeded,\s+(\d+) failed,\s+(\d+) errored,\s+(\d+) timeout", output)
    statuses = re.search(r"status codes:\s+(\d+) 2xx,\s+(\d+) 3xx,\s+"
                         r"(\d+) 4xx,\s+(\d+) 5xx", output)
    if not counts or not statuses:
        raise ValueError("missing h2load request/status summary")
    total, started, done, succeeded, failed, errored, timeout = map(int, counts.groups())
    ok = int(statuses[1])
    http_responses = sum(map(int, statuses.groups()))
    if not done or not ok:
        raise ValueError("no successful responses")
    latencies = []
    for line in log.read_text().splitlines():
        columns = line.split()
        if len(columns) >= 3 and columns[1] == "200":
            latencies.append(int(columns[2]))
    if not latencies:
        raise ValueError("no HTTP 200 latency samples")
    latencies.sort()
    return {
        "total": total, "started": started, "done": done,
        "http_2xx": ok, "succeeded": succeeded, "failed": failed,
        "errored": errored, "timeout": timeout,
        "http_error_rate": (http_responses - ok) / max(1, http_responses),
        "request_failure_rate": failed / max(1, succeeded + failed),
        "network_error_rate": errored / max(1, succeeded + failed),
        "rps_http_2xx": ok / duration,
        "latency_samples": len(latencies),
        "latency_ms": {"mean": statistics.fmean(latencies) / 1000,
                       "p50": percentile(latencies, .50),
                       "p95": percentile(latencies, .95),
                       "p99": percentile(latencies, .99),
                       "max": latencies[-1] / 1000},
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--server-pid", type=int, required=True)
    parser.add_argument("--server-binary", type=Path, required=True)
    parser.add_argument("--revision", required=True, help="exact source commit, plus dirty state if applicable")
    parser.add_argument("--server-cpus", required=True)
    parser.add_argument("--client-cpus", required=True)
    parser.add_argument("--url", default="http://127.0.0.1:18080")
    parser.add_argument("--connections", type=int, choices=[256, 1024], required=True)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--h2load", default="h2load")
    parser.add_argument("--cert", type=Path, help="TLS trust root for the grpcio correctness canary")
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    if min(args.threads, args.duration, args.warmup) <= 0:
        parser.error("threads, duration and warmup must be positive")
    server_cpus, client_cpus = cpus(args.server_cpus), cpus(args.client_cpus)
    if server_cpus & client_cpus:
        parser.error("server and client CPUs must be disjoint")
    available = os.sched_getaffinity(0)
    if not (server_cpus | client_cpus) <= available:
        parser.error("CPU set is unavailable")
    url = urlsplit(args.url)
    if url.scheme not in {"http", "https"} or not url.hostname or not url.port:
        parser.error("--url requires http(s)://host:port")
    if url.scheme == "https" and not args.cert:
        parser.error("TLS runs require --cert for the grpcio correctness canary")
    # Check every thread, not just the main process: accepting an unpinned
    # worker invalidates the comparison even when its parent is pinned.
    for thread in Path(f"/proc/{args.server_pid}/task").iterdir():
        if not os.sched_getaffinity(int(thread.name)) <= server_cpus:
            parser.error(f"server thread {thread.name} is outside server CPU set")
    actual_binary = Path(f"/proc/{args.server_pid}/exe").resolve()
    if actual_binary != args.server_binary.resolve():
        parser.error("server PID does not match --server-binary")
    args.output.mkdir(parents=True, exist_ok=False)
    frame = args.output / "grpc-sum.bin"
    frame.write_bytes(bytes.fromhex("000000000408011002"))
    command = ["taskset", "-c", args.client_cpus, args.h2load,
               args.url.rstrip("/") + "/benchmark.BenchmarkService/GetSum",
               "-d", str(frame), "-H", "content-type: application/grpc",
               "-H", "te: trailers", "-c", str(args.connections),
               "-m", "100", "-t", str(args.threads), "--alpn-list=h2"]
    if args.url.startswith("http://"):
        command += ["-p", "h2c"]
    report = {
        "environment": {"platform": platform.platform(),
                        "revision": args.revision,
                        "cpuinfo": Path("/proc/cpuinfo").read_text(),
                        "h2load": subprocess.check_output([args.h2load, "--version"], text=True).strip(),
                        "server_sha256": hashlib.sha256(actual_binary.read_bytes()).hexdigest(),
                        "server_cmdline": Path(f"/proc/{args.server_pid}/cmdline").read_bytes().decode().replace("\0", " "),
                        "server_cpus": sorted(server_cpus), "client_cpus": sorted(client_cpus)},
        "parameters": {"connections": args.connections, "streams_per_connection": 100,
                       "duration_seconds": args.duration, "warmup_seconds": args.warmup,
                       "request_bytes": 9, "response_bytes": 7},
        "grpc_status": "grpcio canary before/after each round; h2load itself checks HTTP only",
        "comparison_eligible": False, "rounds": [],
    }
    check = [sys.executable, str(Path(__file__).with_name("grpc_arena_check.py")),
             url.netloc]
    if args.cert:
        check += ["--cert", str(args.cert)]
    for iteration in range(1, 4):
        subprocess.run(check, check=True, timeout=20)
        warmup = command + ["-D", str(args.warmup)]
        with (args.output / f"{iteration}-warmup.txt").open("w") as out:
            subprocess.run(warmup, stdout=out, stderr=subprocess.STDOUT,
                           check=True, timeout=args.warmup + 30)
        log = args.output / f"{iteration}-requests.tsv"
        run = command + ["-D", str(args.duration), "--log-file=" + str(log)]
        before_cpu, before_wall = cpu_seconds(args.server_pid), time.monotonic()
        result = subprocess.run(run, capture_output=True, text=True,
                                check=True, timeout=args.duration + 30)
        elapsed = time.monotonic() - before_wall
        used = cpu_seconds(args.server_pid) - before_cpu
        (args.output / f"{iteration}-h2load.txt").write_text(result.stdout + result.stderr)
        measured = parse_result(result.stdout, log, args.duration)
        measured.update(command=shlex.join(run), warmup_command=shlex.join(warmup),
                        server_cpu_seconds=used, command_wall_seconds=elapsed,
                        server_cpu_percent=100 * used / elapsed,
                        grpc_canary_before=True, grpc_canary_after=False)
        report["rounds"].append(measured)
        (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        print(json.dumps(measured), flush=True)
        if measured["failed"] or measured["http_2xx"] != measured["done"]:
            raise RuntimeError("load errors: configuration is not eligible for comparison")
        subprocess.run(check, check=True, timeout=20)
        measured["grpc_canary_after"] = True
        (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
        time.sleep(2)
    report["median_rps"] = statistics.median(r["rps_http_2xx"] for r in report["rounds"])
    report["best_rps"] = max(r["rps_http_2xx"] for r in report["rounds"])
    report["comparison_eligible"] = True
    (args.output / "results.json").write_text(json.dumps(report, indent=2) + "\n")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
