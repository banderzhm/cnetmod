#!/bin/bash
set -euo pipefail
root=/root/cnetmod-ws-20261020
repo=/mnt/c/Users/25007/.codex/worktrees/3aad/cnetmod
for path in CMakeLists.txt src/protocol/http/v1/http_v1_parser.cppm src/protocol/http/v1/http_v1_parser.cpp src/protocol/http/v1/http_v1_parser_buffered.cpp src/protocol/websocket/wire/websocket_frame.cppm src/protocol/websocket/wire/websocket_frame.cpp src/protocol/websocket/client/websocket_client_connection.cppm src/protocol/websocket/client/websocket_client_connection_impl.cpp src/protocol/websocket/server/websocket_server.cppm src/protocol/websocket/server/websocket_server_impl.cpp src/executor/async_op.cppm src/executor/async_op.cpp src/executor/platform/io_uring_async_op.cpp src/executor/platform/epoll_async_op.cpp src/executor/pool.cppm src/executor/server_context_impl.cpp src/coro/spawn.cppm src/io/platform/io_uring_multishot_recv.cpp src/io/platform/io_uring_impl.cpp src/io/platform/io_uring.cppm testing/bench/bench_ws_server.cpp testing/tests/test_websocket_frame.cpp testing/tests/test_stream_write.cpp testing/tests/test_http_parser.cpp testing/tests/CMakeLists.txt; do
  if ! python -c 'from pathlib import Path; import sys; a,b=map(Path,sys.argv[1:]); sys.exit(not(b.exists() and a.read_bytes()==b.read_bytes()))' "$repo/$path" "$root/source/$path"; then
    cp "$repo/$path" "$root/source/$path"
  fi
done
cmake --build "$root/build" --target bench_ws_server test_websocket_frame test_stream_write test_http_parser -j4 > "$root/optimization/build-batch.log" 2>&1
ctest --test-dir "$root/build" -R '^test_(websocket_frame|stream_write|http_parser)$' --output-on-failure --timeout 20 > "$root/optimization/unit.log" 2>&1
python "$repo/testing/bench/check_ws_echo.py" "$root/build/testing/bench/bench_ws_server" > "$root/optimization/wire-ws.log" 2>&1
python "$repo/testing/bench/check_ws_echo.py" "$root/build/testing/bench/bench_ws_server" --cert "$root/cert.pem" --key "$root/key.pem" > "$root/optimization/wire-wss.log" 2>&1
echo 'Batch build and boundary checks passed'
