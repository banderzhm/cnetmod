#!/usr/bin/env bash

set -uo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
server=${CNETMOD_H3_SERVER:-"${repo}/cmake-build-quic-verify-linux/bin/h3_interop_server"}
oha=${CNETMOD_OHA:-/root/.local/cnetmod-oha/bin/oha}
result_dir=${1:-"${repo}/testing/bench/results/crosslang/rust-h3-current"}
cert=${CNETMOD_H3_CERT:-"${repo}/.h3probe/cert.pem"}
key=${CNETMOD_H3_KEY:-"${repo}/.h3probe/key.pem"}
selected=${CNETMOD_RUST_H3_CASES:-all}
base_port=${CNETMOD_H3_BASE_PORT:-19600}
single_requests=${CNETMOD_RUST_H3_SINGLE_REQUESTS:-1000}
single_deadline=${CNETMOD_RUST_H3_SINGLE_DEADLINE:-90s}

mkdir -p "${result_dir}/logs" "${result_dir}/raw"

server_pid=
cleanup()
{
    if [[ -n ${server_pid} ]]; then
        kill "${server_pid}" 2>/dev/null || true
        wait "${server_pid}" 2>/dev/null || true
        server_pid=
    fi
}
trap cleanup EXIT INT TERM

is_selected()
{
    local name=$1
    [[ ${selected} == all || ",${selected}," == *",${name},"* ]]
}

run_case()
{
    local name=$1
    local port=$2
    local workers=$3
    local connections=$4
    local parallel=$5
    local requests=$6
    local deadline=$7
    local prefix="${result_dir}/logs/${name}"
    local output="${result_dir}/raw/${name}.json"

    cleanup
    "${server}" --port "${port}" --workers "${workers}" \
        --cert "${cert}" --key "${key}" \
        >"${prefix}.server.stdout.log" \
        2>"${prefix}.server.stderr.log" &
    server_pid=$!
    sleep 1
    if ! kill -0 "${server_pid}" 2>/dev/null; then
        printf '%s: server failed to start\n' "${name}" | tee "${prefix}.status.log"
        return 2
    fi

    timeout "${deadline}" "${oha}" --no-tui --no-color \
        --http-version 3 --insecure -c "${connections}" -p "${parallel}" \
        -n "${requests}" -t 5s --output-format json --output "${output}" \
        "https://127.0.0.1:${port}/hello" \
        >"${prefix}.client.stdout.log" \
        2>"${prefix}.client.stderr.log"
    local status=$?
    printf '%s: status=%s connections=%s parallel=%s requests=%s\n' \
        "${name}" "${status}" "${connections}" "${parallel}" "${requests}" \
        | tee "${prefix}.status.log"
    cleanup
    return "${status}"
}

failed=0
if is_selected short-reuse; then
    run_case short-reuse "${base_port}" 1 1 1 10 30s || failed=1
fi
if is_selected single-connection; then
    run_case single-connection "$((base_port + 1))" 1 1 16 \
        "${single_requests}" "${single_deadline}" || failed=1
fi
if is_selected sustained; then
    run_case sustained "$((base_port + 2))" 16 16 16 10000 180s || failed=1
fi

exit "${failed}"
