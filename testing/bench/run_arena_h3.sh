#!/usr/bin/env bash
# One phase at a time; this script owns only the process/container it starts.
set -euo pipefail
repo=$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)
phase=${1:?phase: baseline, optimized, or compare}
results=${2:?absolute result directory required}
binary=${CNETMOD_H3_ARENA_SERVER:?set path to Release arena_h3_server}
loadgen=${CNETMOD_H3_LOADGEN:?set path to HTTP3-enabled h2load}
python=${CNETMOD_H3_PYTHON:-python3}
certs=${CNETMOD_H3_CERTS:?directory containing server.crt and server.key}
server_cpus=${CNETMOD_H3_SERVER_CPUS:-0-15}
client_cpus=${CNETMOD_H3_CLIENT_CPUS:-16-31}
workers=${CNETMOD_H3_WORKERS:-16}
pid=
container=
probe_args=()

cleanup()
{
    if [[ -n ${container} ]]; then
        docker logs "${container}" > "${results}/${phase}-server.log" 2>&1 || true
        docker stop -t 2 "${container}" >/dev/null || true
    elif [[ -n ${pid} ]]; then
        kill "${pid}" 2>/dev/null || true
        wait "${pid}" 2>/dev/null || true
    fi
}
trap cleanup EXIT
if pgrep -x 'ninja|h2load|oha|gcannon|wrk|ghz' >/dev/null; then
    echo 'Another build or load generator is running; phase not started.' >&2
    exit 2
fi
mkdir -p "${results}"
case "${phase}" in
    baseline|optimized)
        taskset -c "${server_cpus}" "${binary}" "${certs}/server.crt" \
            "${certs}/server.key" 8443 "${workers}" > "${results}/${phase}-server.log" 2>&1 &
        pid=$!
        ;;
    compare)
        probe_args=(--workload-only)
        image=${CNETMOD_H3_COMPARE_IMAGE:?set an exact comparator image ID}
        container=$(docker run -d --rm --network host --cpuset-cpus "${server_cpus}" \
            --security-opt seccomp=unconfined -e IOXIDE_REACTORS="${workers}" \
            -v "${certs}:/certs:ro" "${image}")
        pid=$(docker inspect --format '{{.State.Pid}}' "${container}")
        docker image inspect "${image}" > "${results}/compare-image.json"
        ;;
    *) echo "unknown phase: ${phase}" >&2; exit 2 ;;
esac
sleep 3
"${python}" "${repo}/testing/bench/arena_h3_probe.py" --port 8443 "${probe_args[@]}" \
    > "${results}/${phase}-correctness.txt" 2>&1
"${python}" "${repo}/testing/bench/arena_h3_measure.py" --pid "${pid}" \
    --loadgen "${loadgen}" --client-cpus "${client_cpus}" \
    --output "${results}/${phase}" | tee "${results}/${phase}-summary.log"
if [[ ${phase} != compare ]]; then
    perf record -F 199 -e cpu-clock:u -g --call-graph dwarf -p "${pid}" \
        -o "${results}/${phase}.perf.data" -- sleep 9 \
        > "${results}/${phase}-perf-record.txt" 2>&1 &
    profiler=$!
    taskset -c "${client_cpus}" "${loadgen}" --alpn-list=h3 \
        --tls13-ciphers=TLS_AES_128_GCM_SHA256 --groups=X25519 \
        'https://127.0.0.1:8443/baseline2?a=1&b=1' \
        -c 64 -m 64 -t 16 -D 5s --warm-up-time=2s \
        > "${results}/${phase}-profile-load.txt" 2>&1
    wait "${profiler}"
    perf report --stdio --no-children --call-graph none --sort dso,symbol \
        --column-widths=12,30,180 \
        -i "${results}/${phase}.perf.data" > "${results}/${phase}-perf-report.txt"
fi
