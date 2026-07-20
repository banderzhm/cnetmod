#!/usr/bin/env python3
"""Python SMTP benchmark for the cnetmod SMTP server.

Each worker reuses one SMTP connection, which measures SMTP command/data path
throughput rather than TCP connection setup.  Pass --new-connection to include
connection establishment in every delivery.
"""

from __future__ import annotations

import argparse
import concurrent.futures
import contextlib
import os
import smtplib
import socket
import statistics
import subprocess
import sys
import threading
import time
from pathlib import Path


HOST = "127.0.0.1"
READY_TIMEOUT_SECONDS = 10.0


def reserve_port() -> int:
    with contextlib.closing(socket.socket(socket.AF_INET, socket.SOCK_STREAM)) as sock:
        sock.bind((HOST, 0))
        return int(sock.getsockname()[1])


def wait_ready(process: subprocess.Popen[str], port: int) -> None:
    deadline = time.monotonic() + READY_TIMEOUT_SECONDS
    while time.monotonic() < deadline:
        if process.poll() is not None:
            raise RuntimeError(f"SMTP benchmark server exited with {process.returncode}")
        try:
            with socket.create_connection((HOST, port), timeout=0.1):
                return
        except OSError:
            time.sleep(0.02)
    raise TimeoutError("SMTP benchmark server did not become ready")


def percentile(values: list[float], fraction: float) -> float:
    if not values:
        return 0.0
    ordered = sorted(values)
    index = min(len(ordered) - 1, round((len(ordered) - 1) * fraction))
    return ordered[index]


def build_message(sequence: int, payload: str) -> str:
    return (
        f"From: benchmark-sender@example.test\r\n"
        f"To: benchmark-recipient@example.test\r\n"
        f"Subject: cnetmod SMTP benchmark {sequence}\r\n"
        f"Message-ID: <{sequence}@bench.cnetmod.test>\r\n"
        f"\r\n{payload}\r\n"
    )


def deliver_range(
    port: int,
    start: int,
    count: int,
    payload: str,
    new_connection: bool,
) -> list[float]:
    durations: list[float] = []
    client: smtplib.SMTP | None = None
    try:
        if not new_connection:
            client = smtplib.SMTP(HOST, port, timeout=10.0)
        for sequence in range(start, start + count):
            if new_connection:
                client = smtplib.SMTP(HOST, port, timeout=10.0)
            assert client is not None
            begin = time.perf_counter_ns()
            refused = client.sendmail(
                "benchmark-sender@example.test",
                ["benchmark-recipient@example.test"],
                build_message(sequence, payload),
            )
            duration = (time.perf_counter_ns() - begin) / 1_000_000.0
            if refused:
                raise RuntimeError(f"recipient rejected: {refused}")
            durations.append(duration)
            if new_connection:
                client.quit()
                client = None
    finally:
        if client is not None:
            with contextlib.suppress(OSError, smtplib.SMTPException):
                client.quit()
    return durations


def main() -> int:
    parser = argparse.ArgumentParser(description="Benchmark cnetmod's SMTP server using Python smtplib")
    parser.add_argument("server", type=Path, help="path to smtp_bench_server executable")
    parser.add_argument("--messages", type=int, default=10_000)
    parser.add_argument("--concurrency", type=int, default=min(32, os.cpu_count() or 1))
    parser.add_argument("--payload-bytes", type=int, default=256)
    parser.add_argument("--new-connection", action="store_true", help="include TCP/SMTP setup in each delivery")
    args = parser.parse_args()

    if args.messages <= 0 or args.concurrency <= 0 or args.payload_bytes < 0:
        parser.error("messages/concurrency must be positive and payload-bytes must not be negative")
    if not args.server.is_file():
        parser.error(f"server executable not found: {args.server}")

    port = reserve_port()
    process = subprocess.Popen(
        [str(args.server), str(port)],
        stdout=subprocess.DEVNULL,
        stderr=subprocess.PIPE,
        text=True,
    )
    try:
        wait_ready(process, port)
        payload = "x" * args.payload_bytes
        workers = min(args.concurrency, args.messages)
        base, remainder = divmod(args.messages, workers)
        assignments = []
        offset = 0
        for worker in range(workers):
            size = base + (1 if worker < remainder else 0)
            assignments.append((offset, size))
            offset += size

        started = time.perf_counter()
        durations: list[float] = []
        lock = threading.Lock()
        with concurrent.futures.ThreadPoolExecutor(max_workers=workers) as pool:
            futures = [
                pool.submit(deliver_range, port, offset, count, payload, args.new_connection)
                for offset, count in assignments
            ]
            for future in concurrent.futures.as_completed(futures):
                result = future.result()
                with lock:
                    durations.extend(result)
        elapsed = time.perf_counter() - started

        print("SMTP Python benchmark")
        print(f"messages={len(durations)} concurrency={workers} payload_bytes={args.payload_bytes}")
        print(f"connection_mode={'new-per-message' if args.new_connection else 'reused-per-worker'}")
        print(f"elapsed_seconds={elapsed:.3f}")
        print(f"throughput_messages_per_second={len(durations) / elapsed:.2f}")
        print(f"latency_ms_mean={statistics.fmean(durations):.3f}")
        print(f"latency_ms_p50={percentile(durations, 0.50):.3f}")
        print(f"latency_ms_p95={percentile(durations, 0.95):.3f}")
        print(f"latency_ms_p99={percentile(durations, 0.99):.3f}")
        return 0
    finally:
        process.terminate()
        try:
            process.wait(timeout=3.0)
        except subprocess.TimeoutExpired:
            process.kill()
            process.wait()
        if process.returncode not in (0, -15):
            stderr = process.stderr.read() if process.stderr else ""
            if stderr:
                print(stderr, file=sys.stderr)


if __name__ == "__main__":
    raise SystemExit(main())
