#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
export DOCKER_HOST=unix:///run/cnetmod-ws-20261020.sock
export GCANNON="$root/gcannon-src/gcannon"
export LOADGEN_DOCKER=false SKIP_TUNE=true
export NO_PROXY=localhost,127.0.0.1,::1 no_proxy=localhost,127.0.0.1,::1
export ZRK_CMD=/usr/bin/false
ulimit -n 65536
cd "$root/HttpArena"
mkdir -p "$root/results/$1"
bash scripts/benchmark.sh "$1" "$2" --save > "$root/results/$1/$2.log" 2>&1
cp -r results "$root/results/$1/selected"
