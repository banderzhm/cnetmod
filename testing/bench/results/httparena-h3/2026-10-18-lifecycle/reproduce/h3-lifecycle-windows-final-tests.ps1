$ErrorActionPreference = 'Stop'
$ctest = 'D:/runtime/vs/Microsoft Visual Studio/18/BuildTools/Common7/IDE/CommonExtensions/Microsoft/CMake/CMake/bin/ctest.exe'
$build = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows'
& $ctest --test-dir $build -C Release -j1 --output-on-failure -R '^(test_quic_(varint|frame|packet|stream|loss_detection|congestion|early_data)|test_http3_(qpack|frame)|h3_webtransport_aioquic_probe_unit|http3_lifecycle|http3_streaming_cancellation|http3_early_data_recovery|http3_idle_connection_cleanup|http_client_http3_.*|http3_multipath_.*|http3_connect_close_end_to_end|http3_connection_migration)$'
exit $LASTEXITCODE

