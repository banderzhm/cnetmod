#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${root}/cmake-build-http2-asan"
cmake_bin="${CMAKE_BIN:-/mnt/f/runtime/cmake/cmake-3.31.4-linux-x86_64/bin/cmake}"

"${cmake_bin}" -S "${root}" -B "${build_dir}" -G Ninja \
  -DCMAKE_BUILD_TYPE=RelWithDebInfo \
  -DCMAKE_C_COMPILER="${CC:-clang-22}" \
  -DCMAKE_CXX_COMPILER="${CXX:-clang++-22}" \
  -DCNETMOD_BUILD_TESTS=ON \
  -DCMAKE_CXX_FLAGS="-fsanitize=address,leak,undefined -fno-omit-frame-pointer" \
  -DCMAKE_EXE_LINKER_FLAGS="-fsanitize=address,leak,undefined"
"${cmake_bin}" --build "${build_dir}" --target test_http_parser test_http2_hpack http2_interop_server http2_client_interop -j "${CMAKE_BUILD_PARALLEL_LEVEL:-8}"

export ASAN_OPTIONS="${ASAN_OPTIONS:-detect_leaks=1:halt_on_error=1}"
export LSAN_OPTIONS="${LSAN_OPTIONS:-exitcode=23}"
"${build_dir}/testing/tests/test_http2_hpack"
"${build_dir}/testing/tests/test_http_parser"
"${root}/.venv-http2/bin/python" "${root}/testing/http2/h2_interop.py" \
  "${build_dir}/testing/tests/http2_interop_server"
BUILD_DIR="${build_dir}" "${root}/testing/http2/run_interop.sh"
