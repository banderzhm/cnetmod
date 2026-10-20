#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
out="$root/optimization/asan"
mkdir -p "$out"
cmake -S "$root/source" -B "$root/build-asan" -G Ninja \
 -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
 '-DCMAKE_CXX_FLAGS_RELEASE=-O1 -g -DNDEBUG -fsanitize=address,undefined -fno-omit-frame-pointer' \
 -DCNETMOD_USE_MIMALLOC=OFF \
 -DCNETMOD_ENABLE_HTTP=ON -DCNETMOD_ENABLE_WEBSOCKET=ON \
 -DCNETMOD_ENABLE_ORM=OFF -DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF \
 -DCNETMOD_BUILD_TESTS=ON -DCNETMOD_BUILD_BENCH=ON -DCNETMOD_CPU_TARGET=portable \
 > "$out/configure.log" 2>&1
cmake --build "$root/build-asan" --target bench_ws_server test_websocket_frame test_stream_write test_http_parser -j4 > "$out/build.log" 2>&1
export ASAN_OPTIONS=detect_leaks=1:halt_on_error=1 UBSAN_OPTIONS=halt_on_error=1:print_stacktrace=1
ctest --test-dir "$root/build-asan" -R '^test_(websocket_frame|stream_write|http_parser)$' --output-on-failure --timeout 30 > "$out/tests.log" 2>&1
server="$root/build-asan/testing/bench/bench_ws_server"
python "$repo/testing/bench/check_ws_echo.py" "$server" --server-log "$out/echo-server.log" > "$out/echo.log" 2>&1
python "$repo/testing/bench/check_ws_protocol.py" "$server" --server-log "$out/protocol-server.log" > "$out/protocol.log" 2>&1
python "$repo/testing/bench/check_ws_protocol.py" "$server" --cert "$root/cert.pem" --key "$root/key.pem" --server-log "$out/protocol-tls-server.log" > "$out/protocol-tls.log" 2>&1
echo 'ASAN/UBSAN unit and wire checks passed'
