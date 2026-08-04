#!/usr/bin/env bash

set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
server=${CNETMOD_H3_SERVER:-"${repo}/cmake-build-quic-verify-linux/bin/h3_interop_server"}
oha=${CNETMOD_OHA:-/root/.local/cnetmod-oha/bin/oha}
profile=${CNETMOD_H3_PERF_OUTPUT:-/tmp/cnetmod-h3.perf}
report=${CNETMOD_H3_PERF_REPORT:-/tmp/cnetmod-h3-perf.txt}
port=${CNETMOD_H3_PROFILE_PORT:-19431}

timeout --signal=INT 10s perf record -e cpu-clock -F 997 -g \
    --call-graph dwarf -o "${profile}" -- \
    taskset -c 0-15 "${server}" --port "${port}" --workers 16 \
    --cert "${repo}/.h3probe/cert.pem" --key "${repo}/.h3probe/key.pem" &
profile_pid=$!

sleep 2
taskset -c 16-31 "${oha}" --no-tui --no-color --http-version 3 \
    -c 16 -p 16 -t 5s --insecure -n 100000 \
    "https://127.0.0.1:${port}/hello" >/dev/null

wait "${profile_pid}" || true
perf report -i "${profile}" --stdio --percent-limit 0.5 \
    --no-children >"${report}"
sed -n '1,180p' "${report}"
