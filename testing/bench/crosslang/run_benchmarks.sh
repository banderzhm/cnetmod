#!/usr/bin/env bash

set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
server_cpus=${CNETMOD_BENCH_SERVER_CPUS:-0-15}
client_cpus=${CNETMOD_BENCH_CLIENT_CPUS:-16-31}
duration=${CNETMOD_BENCH_DURATION:-10s}
warmup=${CNETMOD_BENCH_WARMUP:-3s}
runs=${CNETMOD_BENCH_RUNS:-3}
h1_requests=${CNETMOD_BENCH_H1_REQUESTS:-1000000}
h2_requests=${CNETMOD_BENCH_H2_REQUESTS:-250000}
h3_requests=${CNETMOD_BENCH_H3_REQUESTS:-10000}
h3_connections=${CNETMOD_BENCH_H3_CONNECTIONS:-16}
h3_parallel=${CNETMOD_BENCH_H3_PARALLEL:-16}
h3_workers=${CNETMOD_BENCH_H3_WORKERS:-16}
cnetmod_server_preload=${CNETMOD_BENCH_CNETMOD_SERVER_PRELOAD:-}
cnetmod_iouring_coop=${CNETMOD_BENCH_IOURING_COOP_TASKRUN:-1}
cnetmod_affinity=${CNETMOD_BENCH_CNETMOD_AFFINITY:-0}
cnetmod_minimal_headers=${CNETMOD_BENCH_CNETMOD_MINIMAL_HEADERS:-1}
result_dir=${CNETMOD_BENCH_RESULT_DIR:-"${repo}/testing/bench/results/crosslang/2026-08-04-wsl"}
selected=${CNETMOD_BENCH_SCENARIOS:-all}
java_bin=${CNETMOD_JAVA:-java}
experimental_jetty_h3=${CNETMOD_BENCH_ENABLE_EXPERIMENTAL_JETTY_H3:-0}

cert="${repo}/.h3probe/cert.pem"
key="${repo}/.h3probe/key.pem"
key_store=/root/.local/cnetmod-crosslang.p12
jetty_classpath="${repo}/testing/bench/crosslang/java-jetty/target/classes:${repo}/testing/bench/crosslang/java-jetty/target/dependency/*"
rust_server="${repo}/testing/bench/crosslang/rust/target/release/cnetmod-crosslang-rust"
go_server="${repo}/testing/bench/crosslang/go/crosslang-go"
statico=/root/.local/cnetmod-statico/bin/statico
oha_bin=${CNETMOD_OHA:-/root/.local/cnetmod-oha/bin/oha}
cnetmod_h3_server=${CNETMOD_H3_SERVER:-"${repo}/cmake-build-quic-verify-linux/bin/h3_interop_server"}

require_file()
{
    local path=$1
    local hint=$2
    if [[ ! -e ${path} ]]; then
        printf 'missing %s; %s\n' "${path}" "${hint}" >&2
        exit 2
    fi
}

require_file "${cert}" 'create .h3probe/cert.pem and .h3probe/key.pem first'
require_file "${key}" 'create .h3probe/cert.pem and .h3probe/key.pem first'
if ! command -v "${oha_bin}" >/dev/null 2>&1 && [[ ! -x ${oha_bin} ]]; then
    printf 'missing oha executable: %s (set CNETMOD_OHA)\n' "${oha_bin}" >&2
    exit 2
fi

mkdir -p "${result_dir}/raw" "${result_dir}/logs"
openssl pkcs12 -export -out "${key_store}" -inkey "${key}" -in "${cert}" \
    -passout pass:changeit >/dev/null 2>&1
ulimit -n 1048576

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
    local id=$1
    if [[ ${id} == java26-jetty-h3 && ${selected} == all && ${experimental_jetty_h3} == 0 ]]; then
        return 1
    fi
    [[ ${selected} == all || ",${selected}," == *",${id},"* ]]
}

start_server()
{
    local id=$1
    local port=$2
    local log_prefix="${result_dir}/logs/${id}"
    local -a cnetmod_affinity_option=()
    if [[ ${cnetmod_affinity} != 0 ]]; then
        cnetmod_affinity_option=(--affinity)
    fi
    local -a cnetmod_header_option=()
    if [[ ${cnetmod_minimal_headers} != 0 ]]; then
        cnetmod_header_option=(--minimal-headers)
    fi

    case "${id}" in
        cnetmod-h1)
            taskset -c "${server_cpus}" env \
                CNETMOD_IOURING_COOP_TASKRUN="${cnetmod_iouring_coop}" \
                "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
                --port "${port}" --workers 16 "${cnetmod_affinity_option[@]}" \
                "${cnetmod_header_option[@]}"
            ;;
        rust-hyper-h1)
            taskset -c "${server_cpus}" env TOKIO_WORKER_THREADS=16 \
                "${rust_server}" --http1 --port "${port}"
            ;;
        statico-tokio-uring-h1)
            taskset -c "${server_cpus}" "${statico}" --runtime tokio-uring \
                --threads 16 --ports "${port}" --address 127.0.0.1 --bind-all \
                --body "Hello, World!"
            ;;
        statico-monoio-h1)
            taskset -c "${server_cpus}" "${statico}" --runtime monoio \
                --threads 16 --ports "${port}" --address 127.0.0.1 --bind-all \
                --body "Hello, World!"
            ;;
        go-net-http-h1)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode http1 --port "${port}"
            ;;
        go-fasthttp-h1)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode fasthttp --port "${port}"
            ;;
        java26-virtual-h1)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -Djdk.virtualThreadScheduler.parallelism=16 \
                -cp "${repo}/testing/bench/crosslang/java" JdkVirtualThreadServer \
                --port "${port}"
            ;;
        java26-jetty-h1)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -cp "${jetty_classpath}" dev.cnetmod.bench.JettyServer \
                --mode http1 --port "${port}"
            ;;
        cnetmod-h1-tls)
            taskset -c "${server_cpus}" env \
                CNETMOD_IOURING_COOP_TASKRUN="${cnetmod_iouring_coop}" \
                "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
                --port "${port}" --workers 16 "${cnetmod_affinity_option[@]}" \
                "${cnetmod_header_option[@]}" \
                --tls --cert "${cert}" --key "${key}"
            ;;
        rust-hyper-h1-tls)
            taskset -c "${server_cpus}" env TOKIO_WORKER_THREADS=16 \
                "${rust_server}" --http1 --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        go-net-http-h1-tls)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode http1 --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        go-fasthttp-h1-tls)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode fasthttp --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        java26-virtual-h1-tls)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -Djdk.virtualThreadScheduler.parallelism=16 \
                -cp "${repo}/testing/bench/crosslang/java" JdkVirtualThreadServer \
                --port "${port}" --keystore "${key_store}" --password changeit
            ;;
        java26-jetty-h1-tls)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -cp "${jetty_classpath}" dev.cnetmod.bench.JettyServer \
                --mode http1 --port "${port}" --keystore "${key_store}" --password changeit
            ;;
        cnetmod-h2c)
            taskset -c "${server_cpus}" env \
                CNETMOD_IOURING_COOP_TASKRUN="${cnetmod_iouring_coop}" \
                "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
                --port "${port}" --workers 16 "${cnetmod_affinity_option[@]}" \
                "${cnetmod_header_option[@]}" --http2
            ;;
        rust-hyper-h2c)
            taskset -c "${server_cpus}" env TOKIO_WORKER_THREADS=16 \
                "${rust_server}" --http2 --port "${port}"
            ;;
        go-net-http-h2c)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode http2 --port "${port}"
            ;;
        rust-monoio-h2c)
            taskset -c "${server_cpus}" \
                "${repo}/testing/bench/crosslang/rust/target/release/monoio_h2" \
                --port "${port}" --workers 16
            ;;
        java26-jetty-h2c)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -cp "${jetty_classpath}" dev.cnetmod.bench.JettyServer \
                --mode http2 --port "${port}"
            ;;
        cnetmod-h2-tls)
            taskset -c "${server_cpus}" env \
                CNETMOD_IOURING_COOP_TASKRUN="${cnetmod_iouring_coop}" \
                "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
                --port "${port}" --workers 16 "${cnetmod_affinity_option[@]}" \
                "${cnetmod_header_option[@]}" \
                --http2 --tls --cert "${cert}" --key "${key}"
            ;;
        rust-hyper-h2-tls)
            taskset -c "${server_cpus}" env TOKIO_WORKER_THREADS=16 \
                "${rust_server}" --http2 --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        go-net-http-h2-tls)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode http2 --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        java26-jetty-h2-tls)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -cp "${jetty_classpath}" dev.cnetmod.bench.JettyServer \
                --mode http2 --port "${port}" --keystore "${key_store}" --password changeit
            ;;
        cnetmod-h3)
            taskset -c "${server_cpus}" env \
                LD_PRELOAD="${cnetmod_server_preload}" \
                CNETMOD_IOURING_COOP_TASKRUN="${cnetmod_iouring_coop}" \
                "${cnetmod_h3_server}" \
                --port "${port}" --workers "${h3_workers}" --cert "${cert}" --key "${key}"
            ;;
        rust-h3-quinn)
            taskset -c "${server_cpus}" env TOKIO_WORKER_THREADS=16 \
                "${rust_server}" --http3 --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        go-quic-go-h3)
            taskset -c "${server_cpus}" env GOMAXPROCS=16 \
                "${go_server}" --mode http3 --port "${port}" --cert "${cert}" --key "${key}"
            ;;
        java26-jetty-h3)
            taskset -c "${server_cpus}" "${java_bin}" -XX:ActiveProcessorCount=16 \
                -cp "${jetty_classpath}" dev.cnetmod.bench.JettyServer \
                --mode http3 --port "${port}" --keystore "${key_store}" --password changeit \
                --pem-dir "${CNETMOD_JETTY_QUICHE_PEM_DIR:-/root/.local/cnetmod-jetty-quiche}"
            ;;
        *)
            printf 'unknown scenario %s\n' "${id}" >&2
            return 2
            ;;
    esac >"${log_prefix}.stdout.log" 2>"${log_prefix}.stderr.log" &
    server_pid=$!
}

oha_options()
{
    local protocol=$1
    case "${protocol}" in
        h1) printf '%s\n' '--http-version' '1.1' '-c' '256' ;;
        h1s) printf '%s\n' '--http-version' '1.1' '-c' '256' '--insecure' ;;
        h2c) printf '%s\n' '--http-version' '2' '-c' '16' '-p' '16' ;;
        h2s) printf '%s\n' '--http-version' '2' '-c' '16' '-p' '16' '--insecure' ;;
        h3) printf '%s\n' '--http-version' '3' '-c' "${h3_connections}" '-p' "${h3_parallel}" '-t' '5s' '--insecure' ;;
    esac
}

request_count()
{
    case "$1" in
        h1|h1s) printf '%s\n' "${h1_requests}" ;;
        h2c|h2s) printf '%s\n' "${h2_requests}" ;;
        h3) printf '%s\n' "${h3_requests}" ;;
    esac
}

run_scenario()
{
    local id=$1
    local protocol=$2
    local port=$3
    local scheme=http
    [[ ${protocol} == h1s || ${protocol} == h2s || ${protocol} == h3 ]] && scheme=https
    local url="${scheme}://127.0.0.1:${port}/hello"
    local -a options=()
    mapfile -t options < <(oha_options "${protocol}")
    local requests
    requests=$(request_count "${protocol}")
    local warmup_requests=$((requests / 10))
    ((warmup_requests < 100)) && warmup_requests=100
    ((warmup_requests > 10000)) && warmup_requests=10000

    printf 'starting %s (%s)\n' "${id}" "${protocol}"
    start_server "${id}" "${port}"
    sleep 1
    if ! kill -0 "${server_pid}" 2>/dev/null; then
        printf '%s failed to start\n' "${id}" >&2
        cat "${result_dir}/logs/${id}.stderr.log" >&2
        return 1
    fi

    taskset -c "${client_cpus}" "${oha_bin}" --no-tui --no-color \
        "${options[@]}" -n "${warmup_requests}" "${url}" >/dev/null

    for run in $(seq 1 "${runs}"); do
        local output="${result_dir}/raw/${id}-run${run}.json"
        taskset -c "${client_cpus}" "${oha_bin}" --no-tui --no-color \
            "${options[@]}" -n "${requests}" \
            --output-format json --output "${output}" "${url}"
        printf '  run %s complete\n' "${run}"
    done
    cleanup
    sleep 1
}

cat >"${result_dir}/environment.txt" <<EOF
date=$(date --iso-8601=seconds)
kernel=$(uname -srvo)
server_cpus=${server_cpus}
client_cpus=${client_cpus}
duration=${duration}
warmup=${warmup}
runs=${runs}
h1_requests=${h1_requests}
h2_requests=${h2_requests}
h3_requests=${h3_requests}
h3_workers=${h3_workers}
cnetmod_server_preload=${cnetmod_server_preload}
cnetmod_iouring_coop_taskrun=${cnetmod_iouring_coop}
cnetmod_affinity=${cnetmod_affinity}
cnetmod_minimal_headers=${cnetmod_minimal_headers}
clang=$(clang++ --version | head -1)
rust=$(rustc --version)
go=$(go version)
java=$(${java_bin} -version 2>&1 | head -1)
oha=$(${oha_bin} --version), cargo feature http3 enabled
curl=$(curl --version | head -1)
EOF
lscpu >>"${result_dir}/environment.txt"

scenarios=(
    'cnetmod-h1|h1|19000'
    'rust-hyper-h1|h1|19001'
    'statico-tokio-uring-h1|h1|19002'
    'statico-monoio-h1|h1|19003'
    'go-net-http-h1|h1|19004'
    'go-fasthttp-h1|h1|19005'
    'java26-virtual-h1|h1|19006'
    'java26-jetty-h1|h1|19007'
    'cnetmod-h1-tls|h1s|19100'
    'rust-hyper-h1-tls|h1s|19101'
    'go-net-http-h1-tls|h1s|19102'
    'go-fasthttp-h1-tls|h1s|19103'
    'java26-virtual-h1-tls|h1s|19104'
    'java26-jetty-h1-tls|h1s|19105'
    'cnetmod-h2c|h2c|19200'
    'rust-hyper-h2c|h2c|19201'
    'go-net-http-h2c|h2c|19202'
    'rust-monoio-h2c|h2c|19203'
    'java26-jetty-h2c|h2c|19204'
    'cnetmod-h2-tls|h2s|19300'
    'rust-hyper-h2-tls|h2s|19301'
    'go-net-http-h2-tls|h2s|19302'
    'java26-jetty-h2-tls|h2s|19303'
    'cnetmod-h3|h3|19400'
    'rust-h3-quinn|h3|19401'
    'go-quic-go-h3|h3|19402'
    'java26-jetty-h3|h3|19403'
)

for scenario in "${scenarios[@]}"; do
    IFS='|' read -r id protocol port <<<"${scenario}"
    is_selected "${id}" || continue
    run_scenario "${id}" "${protocol}" "${port}"
done
