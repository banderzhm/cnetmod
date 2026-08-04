#!/usr/bin/env bash

set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
port=${CNETMOD_H3_DIAG_PORT:-20001}
requests=${CNETMOD_H3_DIAG_REQUESTS:-1000}
parallel=${CNETMOD_H3_DIAG_PARALLEL:-16}
result_dir=${1:-"${repo}/testing/bench/results/crosslang/quinn-h3-diag"}
server=${CNETMOD_H3_SERVER:-"${repo}/cmake-build-quic-verify-linux/bin/h3_interop_server"}
client="${repo}/testing/bench/crosslang/rust/target/release/h3_diag_client"
cert="${repo}/.h3probe/cert.pem"
key="${repo}/.h3probe/key.pem"

mkdir -p "${result_dir}"
CNETMOD_QUIC_DIAG=${CNETMOD_QUIC_DIAG:-1} "${server}" \
    --port "${port}" --workers 1 --cert "${cert}" --key "${key}" \
    >"${result_dir}/server.stdout.log" \
    2>"${result_dir}/server.stderr.log" &
server_pid=$!
cleanup()
{
    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
}
trap cleanup EXIT INT TERM

sleep 1
if ! kill -0 "${server_pid}" 2>/dev/null; then
    cat "${result_dir}/server.stderr.log"
    exit 2
fi

RUST_LOG=${RUST_LOG:-quinn=debug} timeout 90s \
    "${client}" "127.0.0.1:${port}" "${cert}" \
    "${requests}" "${parallel}" \
    >"${result_dir}/client.stdout.log" \
    2>"${result_dir}/client.stderr.log"
