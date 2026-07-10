#!/usr/bin/env bash
set -euo pipefail

root="$(cd "$(dirname "${BASH_SOURCE[0]}")/../.." && pwd)"
build_dir="${BUILD_DIR:-${root}/cmake-build-http-refactor-wsl}"
python="${root}/.venv-http2/bin/python"
server="${build_dir}/testing/tests/http2_interop_server"
client="${build_dir}/testing/tests/http2_client_interop"

"${python}" "${root}/testing/http2/h2_interop.py" "${server}"

port=19000
"${python}" "${root}/testing/http2/h2_python_server.py" "${port}" >/tmp/cnetmod-python-h2.log 2>&1 &
pid=$!
trap 'kill "${pid}" 2>/dev/null || true' EXIT
for _ in $(seq 1 100); do
  grep -q "READY ${port}" /tmp/cnetmod-python-h2.log && break
  sleep 0.02
done
"${client}" "${port}"
wait "${pid}"
echo "native HTTP/2 client ↔ python-h2 server: PASS"
