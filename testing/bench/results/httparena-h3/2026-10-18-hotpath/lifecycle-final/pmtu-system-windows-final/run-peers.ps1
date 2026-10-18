param([Parameter(Mandatory=$true)][string]$Evidence)
$ErrorActionPreference = 'Stop'
$py = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows-python/Scripts/python.exe'
$source = 'C:/Users/25007/.codex/worktrees/2c3c/cnetmod'
$server = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows/bin/Release/h3_interop_server.exe'
$client = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows/bin/Release/h3_interop_client.exe'
$rust = 'C:/Users/25007/.codex/tmp/cnetmod-h3-wtransport/target/release/webtransport_probe.exe'
$optional = 'C:/Users/25007/.codex/tmp/cnetmod-h3-windows-optional-peers/run_three_peers.py'
$env:PATH = 'D:/runtime/miniconda3/Library/bin;' + $env:PATH
foreach ($workers in @(1,2)) {
    & $py "$source/testing/http3/h3_acceptance.py" --server $server --client $client --server-workers $workers --port (4432+$workers) --wtransport-probe $rust --results "$Evidence/mandatory-workers$workers.json" > "$Evidence/mandatory-workers$workers.log" 2>&1
    $peerExit = $LASTEXITCODE
    [ordered]@{stage='mandatory';workers=$workers;exit=$peerExit;time=(Get-Date -Format o)} | ConvertTo-Json -Compress | Add-Content "$Evidence/peer-stages.jsonl"
    if ($peerExit -ne 0) { exit $peerExit }
    $cases = Get-Content -Raw "$Evidence/mandatory-workers$workers.json" | ConvertFrom-Json
    foreach ($name in @('aioquic-server_to_cnetmod-client','aioquic-webtransport_to_cnetmod-server','wtransport-webtransport_to_cnetmod-server')) {
        $matches = @($cases | Where-Object { $_.name -eq $name })
        if ($matches.Count -ne 1 -or $matches[0].status -ne 'passed') { throw "Mandatory peer did not pass: $name workers$workers" }
    }
}
foreach ($workers in @(1,2)) {
    & $py $optional --server $server --client $client --workers $workers --output-parent $Evidence > "$Evidence/optional-workers$workers.log" 2>&1
    $peerExit = $LASTEXITCODE
    [ordered]@{stage='optional';workers=$workers;exit=$peerExit;time=(Get-Date -Format o)} | ConvertTo-Json -Compress | Add-Content "$Evidence/peer-stages.jsonl"
    if ($peerExit -ne 0) { exit $peerExit }
}
$protectedHash = (Get-FileHash -Algorithm SHA256 -LiteralPath "$source/testing/http3/h3-interop-results.json").Hash
if ($protectedHash -ne 'CA41C4CB5ADCC34EE16042E00EA9C6DFB248B9A17649995DBACFEB43B64D8B66') { throw 'Protected repository peer JSON changed' }
