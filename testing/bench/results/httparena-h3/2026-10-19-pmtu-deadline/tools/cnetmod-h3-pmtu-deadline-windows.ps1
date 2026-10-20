param([Parameter(Mandatory=$true)][string]$RecordsDirectory)
$ErrorActionPreference = 'Stop'
$cmake = 'D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = 'D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$build = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows'
$source = 'C:/Users/25007/.codex/worktrees/2c3c/cnetmod'
$records = $RecordsDirectory
New-Item -ItemType Directory -Force -Path $records | Out-Null
& $cmake -S $source -B $build -DCNETMOD_USE_MIMALLOC=OFF > "$records/configure.log" 2>&1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --config Release --target test_udp_sendto_on_cancel quic_retire_stream_server quic_pmtu_cc_server quic_pmtu_deadline_server quic_datagram_cc_server test_concurrent_containers test_deadline test_task_group test_wait_group test_task test_udp_batch test_quic_varint test_quic_frame test_quic_packet test_quic_stream test_quic_loss_detection test_quic_congestion test_quic_early_data test_http3_qpack test_http3_frame h3_interop_server h3_interop_client http3_e2e_server http_client_http3_e2e http3_migration_e2e arena_h3_server -- /m:1 > "$records/build.log" 2>&1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ctest --test-dir $build -C Release -j1 --output-on-failure --stop-on-failure -R '^(quic_pmtu_deadline_.*|quic_datagram_cc_.*|test_udp_sendto_on_cancel|quic_pmtu_cc_large_response_after_loss|quic_retire_stream_notification_lifetime|quic_retire_stream_credit_wakeup|quic_private_outbound_pto|quic_handshake_confirmation|test_concurrent_containers|test_deadline|test_task_group|test_wait_group|test_task|test_udp_batch|test_quic_(varint|frame|packet|stream|loss_detection|congestion|early_data)|test_http3_(qpack|frame)|h3_webtransport_aioquic_probe_unit|http3_outbound_.*|http3_peer_close_.*|http3_listener_restart_.*|http3_lifecycle|http3_cold_listener_cleanup|http3_streaming_cancellation|http3_early_data_recovery|http3_idle_connection_cleanup|http_client_http3_.*|http3_multipath_.*|http3_connect_close_end_to_end|http3_connection_migration)$' > "$records/tests.log" 2>&1
exit $LASTEXITCODE






