#!/usr/bin/env bash
set -euo pipefail
entry_dir=$(cd "$(dirname "$0")" && pwd)
entry=$(basename "$entry_dir")
: "${CNETMOD_WS_BINARY:?set the absolute path of the verified Release bench_ws_server}"
: "${CNETMOD_WS_REVISION:?set the exact cnetmod artifact revision}"
: "${CNETMOD_WS_RUNTIME:?set the immutable runtime image ID or digest}"
case "$CNETMOD_WS_RUNTIME" in
    sha256:*|*@sha256:*) ;;
    *) echo 'CNETMOD_WS_RUNTIME must be an immutable image ID or digest' >&2; exit 2 ;;
esac
test -x "$CNETMOD_WS_BINARY"
cp "$CNETMOD_WS_BINARY" "$entry_dir/bench_ws_server"
sha256sum "$entry_dir/bench_ws_server"
docker build --build-arg "CNETMOD_WS_RUNTIME=$CNETMOD_WS_RUNTIME" \
    --label "org.opencontainers.image.revision=$CNETMOD_WS_REVISION" \
    -t "httparena-$entry" "$entry_dir"
