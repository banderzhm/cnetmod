#!/usr/bin/env python3
"""Supplementary request-log quantiles with the pinned official h2load image.

These runs add logging cost and never replace the official three-round headline.
The workload argv and server launcher come from the staged official functions.
"""
import argparse
import gzip
import hashlib
import json
import math
import os
from pathlib import Path
import re
import statistics
import subprocess
import sys

from analyze_official_grpc import parse_log
from grpc_quiet_host import QuietHost

SCRIPT = r'''#!/bin/bash
set -euo pipefail
stage=$1 entry=$2 profile=$3 conns=$4 image=$5 output=$6 python=$7 oracle=$8
cd "$stage"
source scripts/lib/common.sh
source scripts/lib/framework.sh
source scripts/lib/profiles.sh
source scripts/lib/tools/h2load.sh
framework_load_meta "$entry"
parse_profile "${PROFILES[$profile]}"
framework_build
framework_start "$PROF_ENDPOINT" "$PROF_CPU"
trap framework_stop EXIT
framework_wait_ready "$PROF_ENDPOINT"
H2LOAD_CMD="docker run --rm --name cnetmod-grpc-diagnostic-client --network host --cpuset-cpus=$GCANNON_CPUS --security-opt seccomp=unconfined --ulimit memlock=-1:-1 --ulimit nofile=1048576:1048576 -v $REQUESTS_DIR:$REQUESTS_DIR:ro -v $output:$output $image"
mapfile -t cmd < <(h2load_build_args "$PROF_ENDPOINT" "$conns" 1 5s)
cmd+=(--log-file "$output/requests.tsv")
printf '%s\0' "${cmd[@]}" > "$output/argv.nul"
set +e
timeout 45 taskset -c "$GCANNON_CPUS" "${cmd[@]}" > "$output/h2load.stdout" 2>&1
result=$?
set -e
printf '%s\n' "$result" > "$output/client-exit-code"
[ "$result" = 0 ] || exit "$result"
h2load_parse "$PROF_ENDPOINT" "$(cat "$output/h2load.stdout")" > "$output/official-parsed.txt"
if [ "$profile" = unary-grpc-tls ]; then
    "$python" "$oracle" localhost "$H2PORT" --cert "$CERTS_DIR/server.crt" > "$output/pressure-oracle.log" 2>&1
else
    "$python" "$oracle" localhost "$PORT" > "$output/pressure-oracle.log" 2>&1
fi
'''


def quantiles(path):
    values = []
    statuses = {}
    digest = hashlib.sha256()
    size = 0
    with path.open("rb") as source:
        for line in source:
            digest.update(line)
            size += len(line)
            columns = line.split()
            if len(columns) < 3:
                raise ValueError("Incomplete official request-log row")
            status = int(columns[1])
            statuses[status] = statuses.get(status, 0) + 1
            if status == 200:
                duration = int(columns[2])
                if duration < 0:
                    raise ValueError("Negative response latency")
                values.append(duration)
    if not values:
        raise ValueError("No successful request-log samples")
    values.sort()
    result = {name: values[min(len(values) - 1, math.ceil(len(values) * p) - 1)] / 1000
              for name, p in (("p50", .50), ("p95", .95), ("p99", .99))}
    result.update(mean=statistics.fmean(values) / 1000, max=values[-1] / 1000)
    return dict(samples=len(values), status_counts=statuses, latency_ms=result,
                request_log_sha256=digest.hexdigest(), request_log_bytes=size)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--stage", type=Path, required=True)
    parser.add_argument("--images", type=Path, required=True)
    parser.add_argument("--client-image", required=True)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--python", default=sys.executable)
    args = parser.parse_args()
    invocation = json.loads((args.stage / "records/invocation.json").read_text())
    if invocation["official_sha"] != "8977be2721a5aba7b320345cddfcd6f6b654e698":
        raise ValueError("Unpinned official checkout")
    args.output = args.output.resolve()
    args.output.mkdir(parents=True, exist_ok=False)
    script = args.output / "run-one.sh"
    script.write_text(SCRIPT)
    images = json.loads(args.images.read_text())
    results = []
    oracle = Path(__file__).with_name("grpc_arena_h2_check.py").resolve()
    env = dict(os.environ, H2THREADS="64", SKIP_TUNE="true")
    env.pop("GCANNON_CPUS", None)
    with QuietHost(args.output / "quiet-host.json"):
        for name, image in images.items():
            if not re.fullmatch(r"cnetmod-grpc-[a-z0-9-]+", name):
                raise ValueError("Entry must be task scoped")
            for profile in image.get("tests", ("unary-grpc", "unary-grpc-tls")):
                if profile not in ("unary-grpc", "unary-grpc-tls"):
                    raise ValueError("Only the official unary gRPC profiles are supported")
                for count in (256, 1024):
                    output = args.output / f"{name}-{profile}-{count}"
                    output.mkdir()
                    command = ["bash", str(script), str(args.stage.resolve()), name, profile,
                               str(count), args.client_image, str(output), args.python, str(oracle)]
                    if any(re.search(r"\s", item) for item in command[1:]):
                        raise ValueError("The upstream Docker wrapper requires paths without whitespace")
                    with (output / "lifecycle.log").open("wb") as log:
                        subprocess.run(command, stdout=log, stderr=subprocess.STDOUT, env=env,
                                       check=True, timeout=90)
                    stdout = (output / "h2load.stdout").read_text()
                    wrapped = output / "round.log"
                    wrapped.write_text(f"=== {name} / {profile} / {count}c ===\n[run 1/3]\n" + stdout)
                    row = parse_log(wrapped)[0]
                    if not row["valid"] or not row["progressing_beyond_initial_batches"]:
                        raise ValueError(f"Errors or stalled requests in {output}")
                    row.update(quantiles(output / "requests.tsv"))
                    row["diagnostic_only"] = True
                    row["argv"] = (output / "argv.nul").read_bytes().decode().strip("\0").split("\0")
                    raw = output / "requests.tsv"
                    compressed = output / "requests.tsv.gz"
                    with raw.open("rb") as source, gzip.open(compressed, "wb", compresslevel=1) as target:
                        while chunk := source.read(1024 * 1024):
                            target.write(chunk)
                    row["compressed_sha256"] = hashlib.sha256(compressed.read_bytes()).hexdigest()
                    raw.unlink()  # A complete gzip copy and the uncompressed checksum are retained.
                    results.append(row)
                    (args.output / "summary.json").write_text(json.dumps({
                        "note": "One logged diagnostic per case; logging cost excludes these runs from the headline",
                        "official_sha": invocation["official_sha"], "results": results}, indent=2))
                    print(name, profile, count, row["latency_ms"], flush=True)


if __name__ == "__main__":
    main()
