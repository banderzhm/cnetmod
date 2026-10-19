#!/bin/bash
set -euo pipefail
[[ "$(readlink /proc/self/ns/mnt)" != "$(readlink /proc/1/ns/mnt)" ]]
if [[ ! /tmp/cnetmod-h3-arena-build -ef /var/tmp/cnetmod-h3-arena-build ]]; then
 mkdir -p /tmp/cnetmod-h3-arena-build
 mount --bind /var/tmp/cnetmod-h3-arena-build /tmp/cnetmod-h3-arena-build
fi
mkdir -p /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0
cd /tmp/cnetmod-h3-arena-build
grep -q '^CNETMOD_USE_MIMALLOC:BOOL=OFF$' CMakeCache.txt
/var/tmp/cnetmod-ninja-1.11/bin/ninja -j2 test_quic_frame test_quic_loss_detection > /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/build-targeted.log 2>&1
ctest -j1 --output-on-failure --stop-on-failure -R '^(test_quic_frame|test_quic_loss_detection)$' > /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/test-targeted.log 2>&1
cat /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/test-targeted.log
/var/tmp/cnetmod-ninja-1.11/bin/ninja -j2 > /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/build-full.log 2>&1
ctest -j1 --output-on-failure -R '^(test_concurrent_containers|test_deadline|test_task_group|test_task|test_wait_group|test_udp_batch|test_quic_varint|test_quic_frame|test_quic_packet|test_quic_stream|test_quic_loss_detection|test_quic_congestion|test_quic_early_data|test_http3_qpack|test_http3_frame|quic_pmtu_deadline_interval|quic_pmtu_deadline_initial|quic_datagram_cc_admission|quic_datagram_cc_pto|quic_pmtu_cc_large_response_after_loss|http3_lifecycle|http3_cold_listener_cleanup|http3_outbound_pto_close_restart_1|http3_outbound_pto_close_restart_2|quic_retire_stream_credit_wakeup|quic_retire_stream_notification_lifetime|quic_private_outbound_pto|quic_handshake_confirmation|h3_webtransport_aioquic_probe_unit|http3_early_data_recovery|http3_streaming_cancellation|http3_idle_connection_cleanup|http_client_http3_end_to_end|http_client_http3_dynamic_qpack_end_to_end|http3_multipath_end_to_end|http3_multipath_path_mtu_end_to_end|http3_multipath_path_mtu_blackhole_end_to_end|http3_multipath_local_path_close_end_to_end|http3_connect_close_end_to_end|http3_multipath_path_validation_timeout_end_to_end|http3_connection_migration)$' > /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/test-full.log 2>&1
cat /var/tmp/cnetmod-h3-owned-fast-fbaeb04a21614732b24a7fdae850fcd0/test-full.log
