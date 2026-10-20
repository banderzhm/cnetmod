#!/bin/bash
set -euo pipefail
if [[ "$(readlink /proc/self/ns/mnt)" == "$(readlink /proc/1/ns/mnt)" ]]; then
    echo 'Private mount namespace required' >&2
    exit 1
fi
if [[ ! /tmp/cnetmod-h3-arena-build -ef /var/tmp/cnetmod-h3-arena-build ]]; then
    [[ ! -L /tmp/cnetmod-h3-arena-build ]]
    mkdir -p /tmp/cnetmod-h3-arena-build
    [[ -z "$(find /tmp/cnetmod-h3-arena-build -mindepth 1 -maxdepth 1 -print -quit)" ]]
    mount --bind /var/tmp/cnetmod-h3-arena-build /tmp/cnetmod-h3-arena-build
fi
cd /tmp/cnetmod-h3-arena-build
/var/tmp/cnetmod-ninja-1.11/bin/ninja -j2 \
    test_concurrent_containers quic_retire_stream_server \
    test_deadline test_task_group test_wait_group test_task test_udp_batch \
    test_quic_varint test_quic_frame test_quic_packet test_quic_stream \
    test_quic_loss_detection test_quic_congestion test_quic_early_data \
    test_http3_qpack test_http3_frame h3_interop_server h3_interop_client \
    http3_e2e_server http_client_http3_e2e http3_migration_e2e arena_h3_server
