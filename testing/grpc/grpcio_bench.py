#!/usr/bin/env python3

from __future__ import annotations

import argparse
import importlib.util
import json
import pathlib
import socket
import statistics
import subprocess
import sys
import tempfile
import time
from concurrent import futures


def has_module(name: str) -> bool:
    return importlib.util.find_spec(name) is not None


def free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as s:
        s.bind(("127.0.0.1", 0))
        return int(s.getsockname()[1])


def wait_ready(proc: subprocess.Popen[str], timeout: float = 10.0) -> None:
    deadline = time.monotonic() + timeout
    while time.monotonic() < deadline:
        line = proc.stdout.readline() if proc.stdout is not None else ""
        if line.startswith("READY "):
            return
        if proc.poll() is not None:
            stderr = proc.stderr.read() if proc.stderr is not None else ""
            raise RuntimeError(f"server exited early: {proc.returncode}\n{stderr}")
    raise TimeoutError("server did not become ready")


def percentile(values: list[float], p: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    idx = min(len(ordered) - 1, int(round((p / 100.0) * (len(ordered) - 1))))
    return ordered[idx]


def write_report(path: pathlib.Path, data: dict) -> None:
    path.write_text(
        "\n".join(
            [
                "# cnetmod gRPC grpcio Benchmark",
                "",
                f"- Requests: {data['requests']}",
                f"- Concurrency: {data['concurrency']}",
                f"- Duration seconds: {data['duration_seconds']:.6f}",
                f"- Throughput req/s: {data['throughput_rps']:.2f}",
                f"- Latency avg ms: {data['latency_ms']['avg']:.3f}",
                f"- Latency p50 ms: {data['latency_ms']['p50']:.3f}",
                f"- Latency p95 ms: {data['latency_ms']['p95']:.3f}",
                f"- Latency p99 ms: {data['latency_ms']['p99']:.3f}",
                "",
            ]
        ),
        encoding="utf-8",
    )


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("server")
    parser.add_argument("proto")
    parser.add_argument("--requests", type=int, default=10_000)
    parser.add_argument("--concurrency", type=int, default=32)
    parser.add_argument("--json", type=pathlib.Path)
    parser.add_argument("--markdown", type=pathlib.Path)
    args = parser.parse_args()

    if not has_module("grpc") or not has_module("grpc_tools"):
        print("grpcio benchmark requires: pip install grpcio grpcio-tools", file=sys.stderr)
        return 2

    import grpc
    from grpc_tools import protoc

    server_exe = pathlib.Path(args.server)
    proto_file = pathlib.Path(args.proto)
    port = free_port()

    with tempfile.TemporaryDirectory() as tmpdir:
        tmp = pathlib.Path(tmpdir)
        rc = protoc.main(
            [
                "grpc_tools.protoc",
                f"-I{proto_file.parent}",
                f"--python_out={tmp}",
                f"--grpc_python_out={tmp}",
                str(proto_file),
            ]
        )
        if rc != 0:
            raise RuntimeError(f"protoc failed: {rc}")
        sys.path.insert(0, str(tmp))
        import echo_pb2
        import echo_pb2_grpc

        proc = subprocess.Popen(
            [str(server_exe), str(port)],
            text=True,
            stdout=subprocess.PIPE,
            stderr=subprocess.PIPE,
        )
        latencies: list[float] = []
        try:
            wait_ready(proc)
            channel = grpc.insecure_channel(f"127.0.0.1:{port}")
            grpc.channel_ready_future(channel).result(timeout=10)
            stub = echo_pb2_grpc.EchoServiceStub(channel)

            def one(i: int) -> None:
                start = time.perf_counter()
                resp = stub.Say(echo_pb2.EchoRequest(name="bench", sequence=i), timeout=10)
                if resp.sequence != i:
                    raise AssertionError(resp)
                latencies.append((time.perf_counter() - start) * 1000.0)

            start_all = time.perf_counter()
            with futures.ThreadPoolExecutor(max_workers=args.concurrency) as pool:
                list(pool.map(one, range(args.requests)))
            elapsed = time.perf_counter() - start_all
        finally:
            proc.terminate()
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                proc.wait(timeout=5)

    result = {
        "requests": args.requests,
        "concurrency": args.concurrency,
        "duration_seconds": elapsed,
        "throughput_rps": args.requests / elapsed if elapsed else 0.0,
        "latency_ms": {
            "avg": statistics.fmean(latencies) if latencies else 0.0,
            "p50": percentile(latencies, 50),
            "p95": percentile(latencies, 95),
            "p99": percentile(latencies, 99),
        },
    }

    text = json.dumps(result, indent=2)
    print(text)
    if args.json:
        args.json.write_text(text + "\n", encoding="utf-8")
    if args.markdown:
        write_report(args.markdown, result)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
