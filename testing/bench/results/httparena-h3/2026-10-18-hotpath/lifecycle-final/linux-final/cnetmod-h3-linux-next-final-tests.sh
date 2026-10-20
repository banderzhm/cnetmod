#!/bin/bash
set -euo pipefail
ip link set lo up
# This script is entered through the private mount+network wrapper.
if [[ "$(readlink /proc/self/ns/mnt)" == "$(readlink /proc/1/ns/mnt)" ]]; then
    echo 'A private mount namespace is required; refusing host-path mutation.' >&2
    exit 1
fi
if [[ ! /tmp/cnetmod-h3-arena-build -ef /var/tmp/cnetmod-h3-arena-build ]]; then
    [[ ! -L /tmp/cnetmod-h3-arena-build ]]
    mkdir -p /tmp/cnetmod-h3-arena-build
    [[ -z "$(find /tmp/cnetmod-h3-arena-build -mindepth 1 -maxdepth 1 -print -quit)" ]]
    mount --bind /var/tmp/cnetmod-h3-arena-build /tmp/cnetmod-h3-arena-build
fi
cd /mnt/c/Users/25007/.codex/worktrees/2c3c/cnetmod
ctest --test-dir /tmp/cnetmod-h3-arena-build -j1 --output-on-failure --stop-on-failure -R '^(quic_retire_stream_credit_wakeup|quic_private_outbound_pto|quic_handshake_confirmation|test_concurrent_containers|test_deadline|test_task_group|test_wait_group|test_task|test_udp_batch|test_quic_(varint|frame|packet|stream|loss_detection|congestion|early_data)|test_http3_(qpack|frame)|h3_webtransport_aioquic_probe_unit|http3_outbound_.*|http3_peer_close_.*|http3_listener_restart_.*|http3_lifecycle|http3_cold_listener_cleanup|http3_streaming_cancellation|http3_early_data_recovery|http3_idle_connection_cleanup|http_client_http3_.*|http3_multipath_.*|http3_connect_close_end_to_end|http3_connection_migration)$' > /var/tmp/cnetmod-h3-linux-next-final-tests.log 2>&1
/var/tmp/cnetmod-h3-peer/bin/python testing/http3/h3_acceptance.py --server /var/tmp/cnetmod-h3-arena-build/bin/h3_interop_server --client /var/tmp/cnetmod-h3-arena-build/bin/h3_interop_client --wtransport-probe /mnt/c/Users/25007/.codex/tmp/cnetmod-h3-wtransport-linux-target/release/webtransport_probe --nghttp3-client-command '/var/tmp/cnetmod-h3-ngtcp2-peer-build/examples/osslclient --timeout=1s --no-quic-dump --exit-on-first-stream-close 127.0.0.1 {port} {url}' --nghttp3-server-command '/var/tmp/cnetmod-h3-ngtcp2-peer-build/examples/osslserver -q --htdocs {root} 127.0.0.1 {port} {key} {cert}' --results /var/tmp/cnetmod-h3-linux-next-final-six-peers.json > /var/tmp/cnetmod-h3-linux-next-final-six-peers.log 2>&1
/var/tmp/cnetmod-h3-peer/bin/python -c 'import json; r=json.load(open("/var/tmp/cnetmod-h3-linux-next-final-six-peers.json")); assert len(r)==6 and all(x["status"]=="passed" for x in r),r'
