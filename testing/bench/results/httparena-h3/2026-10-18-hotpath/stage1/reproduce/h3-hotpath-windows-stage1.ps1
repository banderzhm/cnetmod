$ErrorActionPreference = 'Stop'
$cmake = 'D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/cmake.exe'
$ctest = 'D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$build = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows'
$source = 'C:/Users/25007/.codex/worktrees/2c3c/cnetmod'
$records = 'C:/Users/25007/.codex/tmp/h3-hotpath-windows-stage1'
New-Item -ItemType Directory -Force -Path $records | Out-Null
& $cmake -S $source -B $build > "$records/configure.log" 2>&1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $cmake --build $build --config Release --target test_quic_varint test_quic_frame test_quic_packet test_quic_stream test_quic_loss_detection test_quic_congestion test_quic_early_data test_http3_qpack test_http3_frame h3_interop_server h3_interop_client http3_e2e_server http_client_http3_e2e http3_migration_e2e arena_h3_server -- /m:1 > "$records/build.log" 2>&1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
& $ctest --test-dir $build -C Release -j1 --output-on-failure -R '^(test_quic_(varint|frame|packet|stream|loss_detection|congestion|early_data)|test_http3_(qpack|frame)|h3_webtransport_aioquic_probe_unit|http3_lifecycle|http3_cold_listener_cleanup|http3_streaming_cancellation|http3_early_data_recovery|http3_idle_connection_cleanup|http_client_http3_.*|http3_multipath_.*|http3_connect_close_end_to_end|http3_connection_migration)$' > "$records/tests.log" 2>&1
exit $LASTEXITCODE
