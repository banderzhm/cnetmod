[CmdletBinding()]
param(
    [ValidateSet('Release', 'RelWithDebInfo')]
    [string]$Configuration = 'RelWithDebInfo',

    [ValidateRange(1, 65535)]
    [int]$Port = 19444,

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
$server = Join-Path $repo "cmake-build-quic-windows\bin\$Configuration\h3_interop_server.exe"
$oha = Join-Path $repo '.oha-windows\bin\oha.exe'
$certificate = Join-Path $repo '.h3probe\cert.pem'
$key = Join-Path $repo '.h3probe\key.pem'
$timestamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$etl = Join-Path $repo "windows-h3-cpu-$timestamp.etl"
$benchmark = Join-Path $repo "windows-h3-benchmark-$timestamp.json"
$serverOut = Join-Path $repo "windows-h3-server-$timestamp.stdout.log"
$serverErr = Join-Path $repo "windows-h3-server-$timestamp.stderr.log"

foreach ($path in @($server, $oha, $certificate, $key)) {
    if (-not (Test-Path -LiteralPath $path)) {
        throw "Required file is missing: $path"
    }
}

if ($Configuration -eq 'Release') {
    Write-Warning 'Release normally has no PDB files. Use RelWithDebInfo for a function-level CPU flame graph.'
}

$serverProcess = $null
$wprStarted = $false

try {
    Write-Host "Starting WPR CPU profile: $etl"
    & wpr.exe -start CPU -filemode
    if ($LASTEXITCODE -ne 0) {
        throw "WPR start failed with exit code $LASTEXITCODE"
    }
    $wprStarted = $true

    $serverArguments = @(
        '--port', $Port,
        '--workers', $Workers,
        '--cert', $certificate,
        '--key', $key
    )
    $serverProcess = Start-Process -FilePath $server -ArgumentList $serverArguments `
        -RedirectStandardOutput $serverOut -RedirectStandardError $serverErr -PassThru

    Start-Sleep -Seconds 2
    if ($serverProcess.HasExited) {
        throw "HTTP/3 server exited during startup. Inspect: $serverErr"
    }

    $url = "https://127.0.0.1:$Port/health"
    Write-Host 'Running warmup: 100000 requests'
    & $oha -n 100000 --http-version 3 -c $Connections -p $Parallel -t 5s `
        --insecure --output-format quiet $url
    if ($LASTEXITCODE -ne 0) {
        throw "HTTP/3 warmup failed with exit code $LASTEXITCODE"
    }

    Write-Host "Running measured benchmark: $Requests requests"
    & $oha -n $Requests --http-version 3 -c $Connections -p $Parallel -t 5s `
        --insecure --output-format json $url | Set-Content -LiteralPath $benchmark -Encoding utf8
    if ($LASTEXITCODE -ne 0) {
        throw "HTTP/3 benchmark failed with exit code $LASTEXITCODE"
    }

    Write-Host "Benchmark result: $benchmark"
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
        Stop-Process -Id $serverProcess.Id -Force
    }

    Write-Host "Server stdout: $serverOut"
    Write-Host "Server stderr: $serverErr"
}
