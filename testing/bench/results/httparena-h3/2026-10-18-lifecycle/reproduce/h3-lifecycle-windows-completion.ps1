$ErrorActionPreference = 'Stop'
& C:/Users/25007/.codex/tmp/h3-lifecycle-windows-final-tests.ps1 > C:/Users/25007/.codex/tmp/h3-lifecycle-windows-final23-tests.log 2>&1
if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
$python = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows-python/Scripts/python.exe'
foreach ($workers in @(1,2)) {
  & $python testing/http3/h3_acceptance.py --server C:/Users/25007/.codex/tmp/cnetmod-h3-windows/bin/Release/h3_interop_server.exe --client C:/Users/25007/.codex/tmp/cnetmod-h3-windows/bin/Release/h3_interop_client.exe --port (45432 + $workers) --server-workers $workers --wtransport-probe C:/Users/25007/.codex/tmp/cnetmod-h3-wtransport/target/release/webtransport_probe.exe --results "C:/Users/25007/.codex/tmp/h3-lifecycle-windows-final23-peer$workers.json" > "C:/Users/25007/.codex/tmp/h3-lifecycle-windows-final23-peer$workers.log" 2>&1
  if ($LASTEXITCODE -ne 0) { exit $LASTEXITCODE }
}
& $python C:/Users/25007/.codex/tmp/h3-lifecycle-memory-regression.py --source C:/Users/25007/.codex/worktrees/2c3c/cnetmod/testing/http3 --server C:/Users/25007/.codex/tmp/cnetmod-h3-windows/bin/Release/h3_interop_server.exe --results C:/Users/25007/.codex/tmp/h3-lifecycle-memory-windows-final.json --rounds 30 --long-streams 10000 > C:/Users/25007/.codex/tmp/h3-lifecycle-memory-windows-final.log 2>&1
exit $LASTEXITCODE
