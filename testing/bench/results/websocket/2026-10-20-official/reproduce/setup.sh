#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
archives=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod/build-ws-evidence
for dep in boringssl json stdexec mimalloc; do
  mkdir -p "$root/source/3rdparty/$dep"
  tar -xf "$archives/$dep.tar" -C "$root/source/3rdparty/$dep"
done
cmake -S "$root/source" -B "$root/build" -G Ninja \
 -DCMAKE_CXX_COMPILER=clang++ -DCMAKE_BUILD_TYPE=Release \
 '-DCMAKE_CXX_FLAGS_RELEASE=-O3 -DNDEBUG -g1 -fno-omit-frame-pointer' \
 -DCNETMOD_ENABLE_HTTP=ON -DCNETMOD_ENABLE_WEBSOCKET=ON \
 -DCNETMOD_ENABLE_ORM=OFF -DCNETMOD_ENABLE_YAML_CONFIGURATION=OFF \
 -DCNETMOD_BUILD_TESTS=ON -DCNETMOD_BUILD_BENCH=ON -DCNETMOD_CPU_TARGET=portable \
 > "$root/configure.log" 2>&1
cmake --build "$root/build" --target bench_ws_server bench_ws_codec test_websocket_frame -j4 > "$root/build.log" 2>&1
mkdir -p "$root/baseline"
cp "$root/build/testing/bench/bench_ws_server" "$root/build/testing/bench/bench_ws_codec" "$root/baseline/"
