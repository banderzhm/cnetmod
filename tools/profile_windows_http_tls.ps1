[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [ValidateSet('http1', 'h2c', 'https1', 'https2')]
    [string]$Protocol = 'https2',

    [ValidateRange(1, 65535)]
    [int]$Port = 19380,

    [ValidateRange(1, 64)]
    [int]$Workers = 16,

    [ValidatePattern('^[1-9][0-9]*([kKmM])?$')]
    [string]$Requests = '1m',

    [ValidateRange(1, 65535)]
    [int]$Connections = 16,

    [ValidateRange(1, 65535)]
    [int]$Parallel = 16
)

$ErrorActionPreference = 'Stop'

function Test-Administrator {
    $identity = [Security.Principal.WindowsIdentity]::GetCurrent()
    $principal = [Security.Principal.WindowsPrincipal]::new($identity)
    return $principal.IsInRole([Security.Principal.WindowsBuiltInRole]::Administrator)
}

if (-not (Test-Administrator)) {
    throw 'Run this script from an elevated PowerShell session. WPR CPU sampling requires administrator rights.'
}

$repo = Split-Path -Parent $PSScriptRoot
$server = Join-Path $repo "cmake-build-quic-windows\testing\bench\$Configuration\crosslang_cnetmod_server.exe"
$oha = Join-Path $repo '.oha-windows\bin\oha.exe'
$certificate = Join-Path $repo '.h3probe\cert.pem'
$key = Join-Path $repo '.h3probe\key.pem'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$stem = "windows-$Protocol-cpu-$timestamp"
$etl = Join-Path $repo "$stem.etl"
$benchmark = Join-Path $repo "$stem.json"
$serverOut = Join-Path $repo "$stem.server.stdout.log"
$serverErr = Join-Path $repo "$stem.server.stderr.log"

foreach ($path in @($server, $oha)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file is missing: $path"
    }
}

$isTls = $Protocol -eq 'https1' -or $Protocol -eq 'https2'
$isHttp2 = $Protocol -eq 'h2c' -or $Protocol -eq 'https2'
if ($isTls) {
    foreach ($path in @($certificate, $key)) {
        if (-not (Test-Path -LiteralPath $path)) {
            throw "Required TLS file is missing: $path"
        }
    }
}

$serverArguments = @('--port', $Port, '--workers', $Workers, '--affinity')
if ($isHttp2) {
    $serverArguments += '--http2'
}
if ($isTls) {
    $serverArguments += @('--tls', '--cert', $certificate, '--key', $key)
}

$scheme = if ($isTls) { 'https' } else { 'http' }
$url = "${scheme}://127.0.0.1:${Port}/hello"
$ohaArguments = @('--no-tui', '--no-color', '--http-version', $(if ($isHttp2) { '2' } else { '1.1' }), '-c', $Connections)
if ($isHttp2) {
    $ohaArguments += @('-p', $Parallel)
}
if ($isTls) {
    $ohaArguments += '--insecure'
}

if ($Configuration -eq 'Release') {
    Write-Warning 'Release normally has no PDB files. Use RelWithDebInfo for a function-level call tree.'
}

$serverProcess = $null
$wprStarted = $false

try {
    $serverProcess = Start-Process -FilePath $server -ArgumentList $serverArguments `
        -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru -WindowStyle Hidden
    Start-Sleep -Seconds 1
    if ($serverProcess.HasExited) {
        throw "Server exited during startup. Inspect: $serverErr"
    }

    Write-Host "Warming $Protocol with 100000 validated requests"
    & $oha @ohaArguments -n 100000 $url | Out-Host
    if ($LASTEXITCODE -ne 0) {
        throw "Warmup failed with exit code $LASTEXITCODE"
    }

    Write-Host "Starting WPR CPU profile: $etl"
    & wpr.exe -start CPU -filemode
    if ($LASTEXITCODE -ne 0) {
        throw "WPR start failed with exit code $LASTEXITCODE"
    }
    $wprStarted = $true

    Write-Host "Running measured $Protocol benchmark: $Requests requests"
    & $oha @ohaArguments -n $Requests --output-format json $url |
        Set-Content -LiteralPath $benchmark -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "Benchmark failed with exit code $LASTEXITCODE"
    }
}
finally {
    if ($wprStarted) {
        Write-Host "Stopping WPR and writing: $etl"
        & wpr.exe -stop $etl
        if ($LASTEXITCODE -ne 0) {
            Write-Warning "WPR stop failed with exit code $LASTEXITCODE"
        }
    }

    if ($null -ne $serverProcess -and -not $serverProcess.HasExited) {
        Stop-Process -Id $serverProcess.Id
    }

    Write-Host "Benchmark result: $benchmark"
    Write-Host "Server stdout: $serverOut"
    Write-Host "Server stderr: $serverErr"
}
