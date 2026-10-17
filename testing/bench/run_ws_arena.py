#!/usr/bin/env python3
"""Linux, serialized HttpArena-style WS measurements. Requires exclusive load token.

The generator's latency is its closed-loop batch latency; pipeline=16 does not
give independent per-frame RTTs. TLS/compression are disabled (gcannon WS mode).
"""
import argparse
import hashlib
import json
import os
from pathlib import Path
import platform
import socket
import subprocess
import time


PROFILES = {"echo": (1, 0), "pipeline": (16, 0), "limited": (1, 10)}


def cpu_set(text):
    result = set()
    for part in text.split(","):
        bounds = [int(value) for value in part.split("-")]
        result.update(range(bounds[0], bounds[-1] + 1))
    return result


def cpu_seconds(pid):
    # comm may contain spaces or parentheses. utime/stime are fields 14/15.
    fields = Path(f"/proc/{pid}/stat").read_text().rsplit(")", 1)[1].split()
    return (int(fields[11]) + int(fields[12])) / os.sysconf("SC_CLK_TCK")


def sha256(path):
    return hashlib.sha256(Path(path).read_bytes()).hexdigest()


def run_client(args, conns, pipeline, quota, size, duration, output):
    command = ["taskset", "-c", args.client_cpus, args.gcannon,
               f"http://127.0.0.1:{args.port}/ws", "--ws", "--ws-msg", "a" * size,
               "-c", str(conns), "-t", str(args.threads), "-d", f"{duration}s",
               "-p", str(pipeline), "--json"]
    if quota:
        command.extend(["-r", str(quota)])
    with output.open("w") as log, output.with_suffix(".stderr").open("w") as err:
        subprocess.run(command, stdout=log, stderr=err, check=True, timeout=duration + 30)
    result = json.loads(output.read_text())
    if result.get("mode") != "websocket" or result.get("responses", 0) <= 0:
        raise RuntimeError(f"no WebSocket responses: {output}")
    if not result.get("latency_us", {}).get("samples", 0):
        raise RuntimeError(f"missing latency samples: {output}")
    expected = {"connections": conns, "threads": args.threads, "pipeline_depth": pipeline}
    for key, value in expected.items():
        if result.get(key) != value:
            raise RuntimeError(f"generator changed {key}: {output}")
    # Preserve even failed samples, then fail the run visibly.
    return result, command


def measure(args, binary, label, name, conns, size, round_number):
    pipeline, quota = PROFILES[name]
    prefix = args.output / f"{label}-{name}-c{conns}-b{size}-r{round_number}"
    server_command = ["taskset", "-c", args.server_cpus, binary, str(args.port), str(args.workers)]
    with prefix.with_suffix(".server.log").open("w") as log:
        server = subprocess.Popen(server_command, stdout=log, stderr=subprocess.STDOUT)
        profiler = None
        try:
            deadline = time.monotonic() + 15
            while True:
                if server.poll() is not None:
                    raise RuntimeError(f"server exited: {prefix}")
                try:
                    with socket.create_connection(("127.0.0.1", args.port), timeout=0.2):
                        break
                except OSError:
                    if time.monotonic() > deadline:
                        raise RuntimeError("server startup timeout")
                    time.sleep(0.05)
            warmup, _ = run_client(args, conns, pipeline, quota, size, args.warmup,
                                   prefix.with_suffix(".warmup.json"))
            if sum(warmup.get("errors", {}).values()) + warmup.get("ws_upgrade_failures", 0):
                raise RuntimeError(f"generator errors during warmup: {prefix}")
            if args.perf:
                perf_log = prefix.with_suffix(".perf.log").open("w")
                profiler = subprocess.Popen(
                    ["perf", "record", "-e", "cpu-clock", "-F", "997", "-g",
                     "--call-graph", "fp", "-p", str(server.pid), "-o", str(prefix) + ".perf.data"],
                    stdout=perf_log, stderr=subprocess.STDOUT)
            cpu_before, start = cpu_seconds(server.pid), time.monotonic()
            result, client_command = run_client(args, conns, pipeline, quota, size, args.duration,
                                                prefix.with_suffix(".raw.json"))
            elapsed = time.monotonic() - start
            consumed = cpu_seconds(server.pid) - cpu_before
            errors = sum(result.get("errors", {}).values()) + result.get("ws_upgrade_failures", 0)
            sample = {"label": label, "profile": name, "connections": conns,
                      "payload_bytes": size, "round": round_number,
                      "pipeline": pipeline, "messages_per_connection": quota,
                      "tls": False, "compression": False,
                      "frames_per_second": result["responses"] / result["elapsed_sec"],
                      "latency_us": result["latency_us"], "errors": errors,
                      "error_event_rate": errors / max(1, result["responses"] + errors),
                      "send_receive_delta_at_stop": result["requests"] - result["responses"],
                      "cpu_seconds": consumed, "cpu_percent_one_core": consumed / elapsed * 100,
                      "measurement_wall_seconds": elapsed, "server_command": server_command,
                      "client_command": client_command}
            prefix.with_suffix(".sample.json").write_text(json.dumps(sample, indent=2) + "\n")
            print(json.dumps({key: sample[key] for key in
                              ("label", "profile", "connections", "payload_bytes", "round",
                               "frames_per_second", "errors", "cpu_percent_one_core")}), flush=True)
            if errors:
                raise RuntimeError(f"generator errors: {prefix}")
            return sample
        finally:
            perf_failed = False
            if profiler:
                profiler.send_signal(2)
                profiler.wait(timeout=10)
                perf_log.close()
                perf_failed = profiler.returncode != 0
            server.terminate()
            try:
                server.wait(timeout=5)
            except subprocess.TimeoutExpired:
                server.kill()
                server.wait()
            if perf_failed:
                raise RuntimeError(f"perf failed: {prefix}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--baseline", required=True)
    parser.add_argument("--candidate", required=True)
    parser.add_argument("--gcannon", required=True)
    parser.add_argument("--baseline-revision", required=True)
    parser.add_argument("--candidate-revision", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--server-cpus", default="0-3")
    parser.add_argument("--client-cpus", default="8-11")
    parser.add_argument("--workers", type=int, default=4)
    parser.add_argument("--threads", type=int, default=4)
    parser.add_argument("--connections", default="512,4096,16384")
    parser.add_argument("--profiles", default="echo,pipeline,limited")
    parser.add_argument("--sizes", default="5")
    parser.add_argument("--duration", type=int, default=5)
    parser.add_argument("--warmup", type=int, default=2)
    parser.add_argument("--rounds", type=int, default=3)
    parser.add_argument("--port", type=int, default=19090)
    parser.add_argument("--perf", action="store_true")
    args = parser.parse_args()
    if min(args.workers, args.threads, args.duration, args.warmup, args.rounds) <= 0:
        parser.error("counts and durations must be positive")
    server_cpus, client_cpus = cpu_set(args.server_cpus), cpu_set(args.client_cpus)
    if server_cpus & client_cpus:
        parser.error("server and client CPU sets must be disjoint")
    if not (server_cpus | client_cpus) <= os.sched_getaffinity(0):
        parser.error("CPU sets exceed available affinity")
    sizes = list(map(int, args.sizes.split(",")))
    connections = list(map(int, args.connections.split(",")))
    profiles = args.profiles.split(",")
    # Pinned gcannon 11c802b has a 256-byte single_frame stack buffer.
    if any(size < 1 or size > 248 for size in sizes):
        parser.error("gcannon payloads must be 1..248 bytes (256-byte frame buffer)")
    if any(count < args.threads for count in connections):
        parser.error("connections must be at least the generator thread count")
    if any(name not in PROFILES for name in profiles):
        parser.error("unknown profile")
    args.output.mkdir(parents=True, exist_ok=False)
    environment = {"arguments": {k: str(v) if isinstance(v, Path) else v for k, v in vars(args).items()},
                   "kernel": platform.uname()._asdict(), "cpuinfo": Path("/proc/cpuinfo").read_text(),
                   "binaries_sha256": {path: sha256(path) for path in
                                       (args.baseline, args.candidate, args.gcannon)},
                   "clock_ticks": os.sysconf("SC_CLK_TCK")}
    (args.output / "environment.json").write_text(json.dumps(environment, indent=2) + "\n")
    samples = []
    for size in sizes:
        for name in profiles:
            for conns in connections:
                if name == "limited" and conns > 4096:
                    continue
                for round_number in range(1, args.rounds + 1):
                    variants = [(args.baseline, "baseline"), (args.candidate, "candidate")]
                    if round_number % 2 == 0:
                        variants.reverse()
                    for binary, label in variants:
                        samples.append(measure(args, binary, label, name, conns, size, round_number))
                        (args.output / "samples.json").write_text(json.dumps(samples, indent=2) + "\n")
                        time.sleep(2)


if __name__ == "__main__":
    main()
