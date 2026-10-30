#!/usr/bin/env bash
# Run the MongoDB contract suites against the developer's local replica set.
set -euo pipefail
script_dir="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")" && pwd)"
project_root="$(cd -- "${script_dir}/.." && pwd)"
build_dir="${1:-${project_root}/build/mongodb-only}"
export CNETMOD_MONGODB_TEST_PORT="${CNETMOD_MONGODB_TEST_PORT:-27017}"
if [[ ! -d "${build_dir}" ]]; then
    echo "Missing CMake build directory: ${build_dir}" >&2
    exit 1
fi
ctest --test-dir "${build_dir}" -R '^test_mongodb_' --no-tests=error --output-on-failure
