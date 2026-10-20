#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
mkdir -p "$root/verification"
ctest --test-dir "$root/build" -R '^test_websocket_frame$' --output-on-failure > "$root/verification/candidate-unit.log" 2>&1
python "$repo/testing/bench/check_ws_echo.py" "$root/candidate/bench_ws_server" > "$root/verification/candidate-ws.log" 2>&1
python "$repo/testing/bench/check_ws_echo.py" "$root/candidate/bench_ws_server" --cert "$root/cert.pem" --key "$root/key.pem" > "$root/verification/candidate-wss.log" 2>&1
for path in src/protocol/websocket/client/websocket_client_connection_impl.cpp src/protocol/websocket/server/websocket_server_impl.cpp testing/tests/test_websocket_frame.cpp; do
  cp "$repo/$path" "$root/source/$path"
done
cmake --build "$root/build" --target bench_ws_server test_websocket_frame -j4 > "$root/verification/restored-build.log" 2>&1
ctest --test-dir "$root/build" -R '^test_websocket_frame$' --output-on-failure > "$root/verification/restored-unit.log" 2>&1
python "$repo/testing/bench/check_ws_echo.py" "$root/build/testing/bench/bench_ws_server" > "$root/verification/restored-ws.log" 2>&1
python "$repo/testing/bench/check_ws_echo.py" "$root/build/testing/bench/bench_ws_server" --cert "$root/cert.pem" --key "$root/key.pem" > "$root/verification/restored-wss.log" 2>&1
python -m unittest discover -s "$repo/testing/bench" -p 'test_*ws*.py' > "$root/verification/python-tests.log" 2>&1
git -C "$root/HttpArena" diff --exit-code -- scripts/benchmark.sh scripts/lib scripts/validate-ws.py > "$root/verification/official-driver-unchanged.log"
bash "$repo/build-ws-evidence/validate_one.sh" cnetmod-ws-baseline > "$root/results/cnetmod-ws-baseline/validation.log" 2>&1
echo 'All final validation passed'
