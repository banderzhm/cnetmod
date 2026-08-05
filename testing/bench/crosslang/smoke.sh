#!/usr/bin/env bash

set -euo pipefail

repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../../.." && pwd)
cert="${repo}/.h3probe/cert.pem"
key="${repo}/.h3probe/key.pem"
key_store=/root/.local/cnetmod-crosslang.p12
java_bin=${CNETMOD_JAVA:-java}
experimental_jetty_h3=${CNETMOD_BENCH_ENABLE_EXPERIMENTAL_JETTY_H3:-0}
openssl pkcs12 -export -out "${key_store}" -inkey "${key}" -in "${cert}" \
    -passout pass:changeit >/dev/null 2>&1

declare -a server_pids=()

cleanup()
{
    if ((${#server_pids[@]} != 0)); then
        kill "${server_pids[@]}" 2>/dev/null || true
        wait "${server_pids[@]}" 2>/dev/null || true
    fi
}
trap cleanup EXIT

smoke_http1()
{
    local name=$1
    local port=$2
    shift 2

    "$@" >"/tmp/cnetmod-${name}.out" 2>"/tmp/cnetmod-${name}.err" &
    local server_pid=$!
    server_pids+=("${server_pid}")

    local body=
    for _ in $(seq 1 100); do
        if body=$(curl --fail --silent --show-error --http1.1 \
            "http://127.0.0.1:${port}/hello" 2>/dev/null); then
            break
        fi
        sleep 0.1
    done

    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
    server_pids=("${server_pids[@]:0:${#server_pids[@]}-1}")

    if [[ ${body} != "Hello, World!" ]]; then
        printf '%s HTTP/1.1 failed; body=[%s]\n' "${name}" "${body}" >&2
        cat "/tmp/cnetmod-${name}.err" >&2
        return 1
    fi
    printf '%s HTTP/1.1 OK\n' "${name}"
}

smoke_h2c()
{
    local name=$1
    local port=$2
    shift 2

    "$@" >"/tmp/cnetmod-${name}.out" 2>"/tmp/cnetmod-${name}.err" &
    local server_pid=$!
    server_pids+=("${server_pid}")

    local body=
    local version=
    for _ in $(seq 1 100); do
        if version=$(curl --fail --silent --show-error --http2-prior-knowledge \
            --output "/tmp/cnetmod-${name}.body" --write-out '%{http_version}' \
            "http://127.0.0.1:${port}/hello" 2>/dev/null); then
            body=$(cat "/tmp/cnetmod-${name}.body")
            break
        fi
        sleep 0.1
    done

    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
    server_pids=("${server_pids[@]:0:${#server_pids[@]}-1}")

    if [[ ${body} != "Hello, World!" || ${version} != "2" ]]; then
        printf '%s h2c failed; HTTP=%s body=[%s]\n' \
            "${name}" "${version}" "${body}" >&2
        cat "/tmp/cnetmod-${name}.err" >&2
        return 1
    fi
    printf '%s h2c OK\n' "${name}"
}

smoke_tls()
{
    local name=$1
    local port=$2
    local expected_version=$3
    shift 3

    "$@" >"/tmp/cnetmod-${name}.out" 2>"/tmp/cnetmod-${name}.err" &
    local server_pid=$!
    server_pids+=("${server_pid}")

    local body=
    local version=
    local -a protocol_option=(--http1.1)
    if [[ ${expected_version} == "2" ]]; then
        protocol_option=(--http2)
    fi
    for _ in $(seq 1 100); do
        if version=$(curl --fail --silent --show-error --insecure \
            "${protocol_option[@]}" \
            --output "/tmp/cnetmod-${name}.body" --write-out '%{http_version}' \
            --resolve "localhost:${port}:127.0.0.1" \
            "https://localhost:${port}/hello" 2>/dev/null); then
            body=$(cat "/tmp/cnetmod-${name}.body")
            break
        fi
        sleep 0.1
    done

    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
    server_pids=("${server_pids[@]:0:${#server_pids[@]}-1}")

    if [[ ${body} != "Hello, World!" || ${version} != "${expected_version}" ]]; then
        printf '%s TLS failed; HTTP=%s body=[%s]\n' \
            "${name}" "${version}" "${body}" >&2
        cat "/tmp/cnetmod-${name}.err" >&2
        return 1
    fi
    printf '%s HTTPS/%s OK\n' "${name}" "${expected_version}"
}

smoke_h3()
{
    local name=$1
    local port=$2
    shift 2

    "$@" >"/tmp/cnetmod-${name}.out" 2>"/tmp/cnetmod-${name}.err" &
    local server_pid=$!
    server_pids+=("${server_pid}")

    local body=
    local version=
    for _ in $(seq 1 30); do
        if version=$(curl --fail --silent --show-error --insecure --http3-only \
            --connect-timeout 1 --max-time 2 \
            --output "/tmp/cnetmod-${name}.body" --write-out '%{http_version}' \
            --resolve "localhost:${port}:127.0.0.1" \
            "https://localhost:${port}/hello" 2>/dev/null); then
            body=$(cat "/tmp/cnetmod-${name}.body")
            break
        fi
        sleep 0.1
    done

    kill "${server_pid}" 2>/dev/null || true
    wait "${server_pid}" 2>/dev/null || true
    server_pids=("${server_pids[@]:0:${#server_pids[@]}-1}")

    if [[ ${body} != "Hello, World!" || ${version} != "3" ]]; then
        printf '%s HTTP/3 failed; HTTP=%s body=[%s]\n' \
            "${name}" "${version}" "${body}" >&2
        cat "/tmp/cnetmod-${name}.err" >&2
        return 1
    fi
    printf '%s HTTP/3 OK\n' "${name}"
}

smoke_http1 cnetmod 18100 \
    "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
    --port 18100 --workers 16
smoke_http1 rust-hyper 18101 \
    "${repo}/testing/bench/crosslang/rust/target/release/cnetmod-crosslang-rust" \
    --http1 --port 18101
smoke_http1 go-net-http 18102 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode http1 --port 18102
smoke_http1 go-fasthttp 18106 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode fasthttp --port 18106
smoke_http1 java26-virtual-thread 18103 \
    "${java_bin}" -cp "${repo}/testing/bench/crosslang/java" JdkVirtualThreadServer \
    --port 18103
smoke_http1 java26-jetty 18107 \
    "${java_bin}" -cp "${repo}/testing/bench/crosslang/java-jetty/target/classes:${repo}/testing/bench/crosslang/java-jetty/target/dependency/*" \
    dev.cnetmod.bench.JettyServer --mode http1 --port 18107
smoke_http1 statico-tokio-uring 18104 \
    /root/.local/cnetmod-statico/bin/statico --runtime tokio-uring --threads 16 \
    --ports 18104 --address 127.0.0.1 --bind-all --body "Hello, World!"
smoke_http1 statico-monoio 18105 \
    /root/.local/cnetmod-statico/bin/statico --runtime monoio --threads 16 \
    --ports 18105 --address 127.0.0.1 --bind-all --body "Hello, World!"

smoke_h2c cnetmod 18200 \
    "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
    --port 18200 --workers 16 --http2
smoke_h2c rust-hyper 18201 \
    "${repo}/testing/bench/crosslang/rust/target/release/cnetmod-crosslang-rust" \
    --http2 --port 18201
smoke_h2c go-net-http 18202 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode http2 --port 18202
smoke_h2c rust-monoio-http 18203 \
    "${repo}/testing/bench/crosslang/rust/target/release/monoio_h2" \
    --port 18203 --workers 16
smoke_h2c java26-jetty 18204 \
    "${java_bin}" -cp "${repo}/testing/bench/crosslang/java-jetty/target/classes:${repo}/testing/bench/crosslang/java-jetty/target/dependency/*" \
    dev.cnetmod.bench.JettyServer --mode http2 --port 18204

smoke_tls cnetmod 18300 1.1 \
    "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
    --port 18300 --workers 16 --tls --cert "${cert}" --key "${key}"
smoke_tls rust-hyper 18301 1.1 \
    "${repo}/testing/bench/crosslang/rust/target/release/cnetmod-crosslang-rust" \
    --http1 --port 18301 --cert "${cert}" --key "${key}"
smoke_tls go-net-http 18302 1.1 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode http1 --port 18302 --cert "${cert}" --key "${key}"
smoke_tls go-fasthttp 18305 1.1 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode fasthttp --port 18305 --cert "${cert}" --key "${key}"
smoke_tls java26-virtual-thread 18303 1.1 \
    "${java_bin}" -cp "${repo}/testing/bench/crosslang/java" JdkVirtualThreadServer \
    --port 18303 --keystore "${key_store}" --password changeit
smoke_tls java26-jetty 18304 1.1 \
    "${java_bin}" -cp "${repo}/testing/bench/crosslang/java-jetty/target/classes:${repo}/testing/bench/crosslang/java-jetty/target/dependency/*" \
    dev.cnetmod.bench.JettyServer --mode http1 --port 18304 \
    --keystore "${key_store}" --password changeit

smoke_tls cnetmod 18400 2 \
    "${repo}/cmake-build-release-wsl/testing/bench/crosslang_cnetmod_server" \
    --port 18400 --workers 16 --http2 --tls --cert "${cert}" --key "${key}"
smoke_tls rust-hyper 18401 2 \
    "${repo}/testing/bench/crosslang/rust/target/release/cnetmod-crosslang-rust" \
    --http2 --port 18401 --cert "${cert}" --key "${key}"
smoke_tls go-net-http 18402 2 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode http2 --port 18402 --cert "${cert}" --key "${key}"
smoke_tls java26-jetty 18403 2 \
    "${java_bin}" -cp "${repo}/testing/bench/crosslang/java-jetty/target/classes:${repo}/testing/bench/crosslang/java-jetty/target/dependency/*" \
    dev.cnetmod.bench.JettyServer --mode http2 --port 18403 \
    --keystore "${key_store}" --password changeit

smoke_h3 cnetmod 18500 \
    "${repo}/cmake-build-quic-verify-linux/bin/h3_interop_server" \
    --port 18500 --workers 16 --cert "${cert}" --key "${key}"
smoke_h3 rust-h3-quinn 18501 \
    "${repo}/testing/bench/crosslang/rust/target/release/cnetmod-crosslang-rust" \
    --http3 --port 18501 --cert "${cert}" --key "${key}"
smoke_h3 go-quic-go 18502 \
    "${repo}/testing/bench/crosslang/go/crosslang-go" \
    --mode http3 --port 18502 --cert "${cert}" --key "${key}"
if [[ ${experimental_jetty_h3} != 0 ]]; then
    smoke_h3 java26-jetty 18503 \
        "${java_bin}" -cp "${repo}/testing/bench/crosslang/java-jetty/target/classes:${repo}/testing/bench/crosslang/java-jetty/target/dependency/*" \
        dev.cnetmod.bench.JettyServer --mode http3 --port 18503 \
        --keystore "${key_store}" --password changeit \
        --pem-dir "${CNETMOD_JETTY_QUICHE_PEM_DIR:-/root/.local/cnetmod-jetty-quiche}"
else
    printf 'java26-jetty HTTP/3 skipped (experimental; set CNETMOD_BENCH_ENABLE_EXPERIMENTAL_JETTY_H3=1)\n'
fi
