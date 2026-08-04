#!/usr/bin/env python3
"""Record reproducible HTTP/3 curl measurements; never manufacture metrics."""

from __future__ import annotations

import argparse
import json
import os
import platform
import shutil
import subprocess
import time
from pathlib import Path

SKIP = 77


def main() -> int:
    parser = argparse.ArgumentParser(description="HTTP/3 performance baseline recorder")
    parser.add_argument("--url", required=True)
    parser.add_argument("--requests", type=int, default=100)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    curl = shutil.which("curl")
    if not curl:
        print("SKIP: curl unavailable")
        return SKIP
    version = subprocess.run([curl, "--version"], text=True, capture_output=True, check=False)
    if "http3" not in version.stdout.lower():
        print("SKIP: curl has no HTTP/3 support")
        return SKIP
    samples: list[float] = []
    failures: list[dict[str, object]] = []
    for index in range(args.requests):
        started = time.perf_counter()
        completed = subprocess.run([curl, "--http3-only", "--insecure", "--silent", "--show-error", "--fail", "--output", os.devnull, args.url], text=True, capture_output=True, check=False)
        elapsed_ms = (time.perf_counter() - started) * 1000
        if completed.returncode:
            failures.append({"request": index, "exit": completed.returncode, "stderr": completed.stderr.strip()})
        else:
            samples.append(elapsed_ms)
    record = {
        "platform": platform.platform(),
        "curl_version": version.stdout.splitlines()[0] if version.stdout else "unknown",
        "url": args.url,
        "requested": args.requests,
        "successful": len(samples),
        "failures": failures,
        "latency_ms": {
            "min": min(samples) if samples else None,
            "p50": sorted(samples)[len(samples) // 2] if samples else None,
            "p99": sorted(samples)[min(len(samples) - 1, int(len(samples) * .99))] if samples else None,
            "max": max(samples) if samples else None,
        },
    }
    args.output.write_text(json.dumps(record, indent=2), encoding="utf-8")
    if failures or not samples:
        print(f"FAILED: {len(samples)}/{args.requests} requests succeeded; details written to {args.output}")
        return 1
    print(f"PASSED: {len(samples)} requests; details written to {args.output}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
