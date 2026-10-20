#!/bin/bash
set -euo pipefail
[[ "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)" ]]
[[ "$(readlink /proc/self/ns/net)" != "$(readlink /proc/1/ns/net)" ]]
if [[ ! /tmp/cnetmod-h3-arena-build -ef /var/tmp/cnetmod-h3-arena-build ]]; then
 mkdir -p /tmp/cnetmod-h3-arena-build
 mount --bind /var/tmp/cnetmod-h3-arena-build /tmp/cnetmod-h3-arena-build
fi
out=/var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/full41-net
mkdir -p "$out"
ip link set lo up
readlink /proc/self/ns/net > "$out/netns.txt"
readlink /proc/self/ns/mnt > "$out/mntns.txt"
ip -j link show lo > "$out/lo-link.json"
ip -j address show lo > "$out/lo-address.json"
ip -j route show table all > "$out/routes.json"
ethtool -k lo > "$out/lo-offloads.txt" 2>&1 || true
cd /tmp/cnetmod-h3-arena-build
ctest -j1 --output-on-failure -R '^(test_concurrent_containers|test_deadline|test_task_group|test_task|test_wait_group|test_udp_batch|test_quic_varint|test_quic_frame|test_quic_packet|test_quic_stream|test_quic_loss_detection|test_quic_congestion|test_quic_early_data|test_http3_qpack|test_http3_frame|quic_pmtu_deadline_interval|quic_pmtu_deadline_initial|quic_datagram_cc_admission|quic_datagram_cc_pto|quic_pmtu_cc_large_response_after_loss|http3_lifecycle|http3_cold_listener_cleanup|http3_outbound_pto_close_restart_1|http3_outbound_pto_close_restart_2|quic_retire_stream_credit_wakeup|quic_retire_stream_notification_lifetime|quic_private_outbound_pto|quic_handshake_confirmation|h3_webtransport_aioquic_probe_unit|http3_early_data_recovery|http3_streaming_cancellation|http3_idle_connection_cleanup|http_client_http3_end_to_end|http_client_http3_dynamic_qpack_end_to_end|http3_multipath_end_to_end|http3_multipath_path_mtu_end_to_end|http3_multipath_path_mtu_blackhole_end_to_end|http3_multipath_local_path_close_end_to_end|http3_connect_close_end_to_end|http3_multipath_path_validation_timeout_end_to_end|http3_connection_migration)$' > "$out/tests.log" 2>&1
cat "$out/tests.log"
cp Testing/Temporary/LastTest.log "$out/LastTest.log"
