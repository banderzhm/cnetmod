#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
export DOCKER_HOST=unix:///run/cnetmod-ws-20261020.sock
export NO_PROXY=localhost,127.0.0.1,::1 no_proxy=localhost,127.0.0.1,::1
if docker image inspect "cnetmod-ws-peer-$1:20261020" >/dev/null 2>&1; then
  docker tag "cnetmod-ws-peer-$1:20261020" "httparena-$1"
fi
ulimit -n 65536
cd "$root/HttpArena"
source scripts/lib/common.sh
source scripts/lib/framework.sh
source scripts/lib/profiles.sh
source scripts/lib/postgres.sh
framework_load_meta "$1"
cleanup() {
  status=$?
  if [ "$status" -ne 0 ]; then
    docker logs --tail 100 "$CONTAINER_NAME" || true
    docker inspect -f '{{json .State}}' "$CONTAINER_NAME" || true
  fi
  framework_stop
  postgres_stop
  exit "$status"
}
trap cleanup EXIT
need_pg=false
for test in async-db gateway-64 gateway-h3 production-stack fortunes; do
  if framework_subscribes_to "$test"; then need_pg=true; break; fi
done
if $need_pg; then postgres_start; fi
framework_start ws-echo '0-31,64-95'
framework_wait_ready ws-echo
python scripts/validate-ws.py localhost 8080 /ws
