#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
mkdir -p "$root/optimization/gates/$1"
python "$repo/testing/bench/check_ws_protocol.py" "$root/build/testing/bench/bench_ws_server" > "$root/optimization/gates/$1/protocol-ws.log" 2>&1
python "$repo/testing/bench/check_ws_protocol.py" "$root/build/testing/bench/bench_ws_server" --cert "$root/cert.pem" --key "$root/key.pem" > "$root/optimization/gates/$1/protocol-wss.log" 2>&1
bash "$repo/build-ws-evidence/validate_one.sh" "$1" > "$root/optimization/gates/$1/official.log" 2>&1
echo "All gates passed: $1"
