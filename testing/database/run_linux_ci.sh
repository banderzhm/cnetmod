#!/usr/bin/env bash
# Disposable loopback-only database deployments for a Linux CI runner.
set -euo pipefail
if [[ "${GITHUB_ACTIONS:-}" != true && "${CNETMOD_LOCAL_CONTAINER_TESTS:-}" != 1 ]]; then
    echo 'This fixture requires GitHub Actions or CNETMOD_LOCAL_CONTAINER_TESTS=1.' >&2
    exit 2
fi
build=$(realpath "${1:?build directory required}")
containers=()
cleanup() {
    status=$?
    trap - EXIT
    for container in "${containers[@]}"; do
        docker rm --force "$container" >/dev/null || status=1
    done
    exit "$status"
}
trap cleanup EXIT
trap 'exit 130' INT
trap 'exit 143' TERM

# Refuse port collisions; never stop a service that this script did not create.
python -c 'import socket; sockets=[]
for port in (25432, 27017, 27018, 27019):
 s=socket.socket(); s.bind(("127.0.0.1", port)); sockets.append(s)'

pg=$(docker run --detach --publish 127.0.0.1:25432:5432 \
    --tmpfs /var/lib/postgresql/data:rw,nosuid,size=512m \
    -e POSTGRES_USER=cnetmod_test -e POSTGRES_PASSWORD=disposable-test-password \
    -e POSTGRES_DB=cnetmod_test postgres:17)
containers+=("$pg")
ready=false
for attempt in $(seq 1 60); do
    if docker exec "$pg" pg_isready -h 127.0.0.1 -U cnetmod_test -d cnetmod_test >/dev/null 2>&1; then
        ready=true
        break
    fi
    sleep 1
done
[[ "$ready" == true ]] || { echo 'PostgreSQL startup timed out.' >&2; exit 1; }

mongo=()
for port in 27017 27018 27019; do
    node=$(docker run --detach --network host --tmpfs /data/db:rw,nosuid,size=512m \
        mongo:8.0 mongod --bind_ip 127.0.0.1 --port "$port" --replSet cnetmod \
        --wiredTigerCacheSizeGB 0.25 --setParameter enableTestCommands=1)
    containers+=("$node")
    mongo+=("$node")
    ready=false
    for attempt in $(seq 1 60); do
        if docker exec "$node" mongosh --quiet --port "$port" --eval 'quit(db.adminCommand({ping:1}).ok ? 0 : 1)' >/dev/null 2>&1; then
            ready=true
            break
        fi
        sleep 1
    done
    [[ "$ready" == true ]] || { echo 'MongoDB startup timed out.' >&2; exit 1; }
done
docker exec "${mongo[0]}" mongosh --quiet --eval \
    'quit(rs.initiate({_id:"cnetmod",members:[{_id:0,host:"127.0.0.1:27017"},{_id:1,host:"127.0.0.1:27018"},{_id:2,host:"127.0.0.1:27019"}],settings:{electionTimeoutMillis:2000}}).ok ? 0 : 1)'
ready=false
for attempt in $(seq 1 90); do
    if docker exec "${mongo[0]}" mongosh --quiet --eval \
        'const members=rs.status().members||[]; quit(members.length===3 && members.every(m=>m.health===1 && (m.stateStr==="PRIMARY" || m.stateStr==="SECONDARY")) ? 0 : 1)' \
        >/dev/null 2>&1; then
        ready=true
        break
    fi
    sleep 1
done
[[ "$ready" == true ]] || { echo 'MongoDB replica set did not become fully electable.' >&2; exit 1; }
export CNETMOD_DATABASE_REQUIRED=1
export CNETMOD_POSTGRESQL_URI='postgresql://cnetmod_test:disposable-test-password@127.0.0.1:25432/cnetmod_test?sslmode=disable'
export CNETMOD_MONGODB_URI='mongodb://127.0.0.1:27017,127.0.0.1:27018,127.0.0.1:27019/cnetmod_interop?replicaSet=cnetmod'
export CNETMOD_MONGODB_FAILPOINT_TEST=1
export CNETMOD_MONGODB_FAILOVER_TEST=1
python -c 'import os; from pymongo import MongoClient
with MongoClient(os.environ["CNETMOD_MONGODB_URI"], serverSelectionTimeoutMS=60000) as client:
 client.admin.command("ping")'
for protocol in postgresql mongodb; do
    ctest --test-dir "$build" --output-on-failure --no-tests=error --timeout 900 \
        -R "^python_${protocol}_interoperability$"
done
export CNETMOD_POSTGRESQL_EXAMPLE="$build/examples/database/postgresql/example_postgresql_production_service"
export CNETMOD_MONGODB_EXAMPLE="$build/examples/database/mongodb/example_mongodb_production_service"
ctest --test-dir "$build" --output-on-failure --no-tests=error --timeout 900 \
    -R '^python_(postgresql|mongodb)_production_example_e2e$'
