#!/usr/bin/env bash

set -euo pipefail

if [[ $# -ne 1 ]]; then
    echo "usage: $0 <test-executable>" >&2
    exit 2
fi

test_executable=$1
readonly first_port=17000
readonly last_port=17005
containers=()

cleanup() {
    local status=$?
    for container in "${containers[@]}"; do
        docker logs "$container" || true
        docker rm --force "$container" >/dev/null 2>&1 || true
    done
    exit "$status"
}
trap cleanup EXIT

for port in $(seq "$first_port" "$last_port"); do
    container=$(docker run --detach --network host \
        --tmpfs /data:rw,noexec,nosuid,size=32m \
        redis:7.4 redis-server \
        --port "$port" \
        --save '' \
        --appendonly no \
        --protected-mode no \
        --cluster-enabled yes \
        --cluster-config-file nodes.conf \
        --cluster-node-timeout 5000 \
        --cluster-announce-ip 127.0.0.1 \
        --cluster-announce-port "$port")
    containers+=("$container")
done

for index in "${!containers[@]}"; do
    port=$((first_port + index))
    ready=false
    for _ in $(seq 1 30); do
        if docker exec "${containers[$index]}" redis-cli -p "$port" ping | grep -qx PONG; then
            ready=true
            break
        fi
        sleep 1
    done
    if [[ $ready != true ]]; then
        echo "Redis Cluster node on port $port did not become ready." >&2
        exit 1
    fi
done

nodes=()
for port in $(seq "$first_port" "$last_port"); do
    nodes+=("127.0.0.1:$port")
done
docker run --rm --network host redis:7.4 redis-cli --cluster create \
    "${nodes[@]}" --cluster-replicas 1 --cluster-yes

export CNETMOD_REDIS_CLUSTER_INTEGRATION=1
export CNETMOD_REDIS_CLUSTER_SEED_PORT=$first_port
timeout 45 "$test_executable"
