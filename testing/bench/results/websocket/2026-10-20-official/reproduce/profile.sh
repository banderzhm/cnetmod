#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
export DOCKER_HOST=unix:///run/cnetmod-ws-20261020.sock
export NO_PROXY=localhost,127.0.0.1,::1 no_proxy=localhost,127.0.0.1,::1
ulimit -n 65536
cd "$root/HttpArena"
source scripts/lib/common.sh
source scripts/lib/framework.sh
source scripts/lib/profiles.sh
source scripts/lib/tools/gcannon.sh
framework_load_meta cnetmod-ws-baseline
trap framework_stop EXIT
framework_start ws-echo '0-31,64-95'
framework_wait_ready ws-echo
pid=$(docker inspect -f '{{.State.Pid}}' "$CONTAINER_NAME")
perf record -e cpu-clock -F 997 -g --call-graph fp -p "$pid" -o "$root/baseline-perf.data" > "$root/baseline-perf-record.log" 2>&1 &
profiler=$!
sleep 1
mapfile -t args < <(gcannon_build_args ws-echo 512 16 10s 0)
GCANNON="$root/gcannon-src/gcannon" gcannon_run "${args[@]}" > "$root/baseline-perf-load.log"
kill -INT "$profiler"
wait "$profiler"
perf report -i "$root/baseline-perf.data" --symfs "$root/runtime-rootfs" --stdio --no-children --percent-limit 0.2 > "$root/baseline-perf-report.txt"
