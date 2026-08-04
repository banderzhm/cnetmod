#!/usr/bin/env bash

set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
server="${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server"
oha=${CNETMOD_OHA:-/root/.local/cnetmod-oha/bin/oha}
profile=${CNETMOD_H1_PERF_OUTPUT:-/tmp/cnetmod-h1.perf}
report=${CNETMOD_H1_PERF_REPORT:-/tmp/cnetmod-h1-perf.txt}
port=${CNETMOD_H1_PROFILE_PORT:-19432}
requests=${CNETMOD_H1_PROFILE_REQUESTS:-10000000}

timeout --signal=INT 10s perf record -e cpu-clock -F 997 -g \
    --call-graph dwarf -o "${profile}" -- \
    taskset -c 0-15 "${server}" --port "${port}" --workers 16 &
profile_pid=$!

sleep 2
taskset -c 16-31 "${oha}" --no-tui --no-color --http-version 1.1 \
    -c 256 -n "${requests}" "http://127.0.0.1:${port}/hello" >/dev/null

wait "${profile_pid}" || true
perf report -i "${profile}" --stdio --percent-limit 0.5 \
    --no-children >"${report}"
sed -n '1,180p' "${report}"
