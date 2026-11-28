# Bounded, reproducible soak test for the HCSUDA runtime.
# Runs hcsbench with small, fixed workloads on one device and checks that every
# benchmark row stays verified against its host reference. No unbounded allocations.
param(
    [int]$Minutes = 60,
    [int]$Device = 0,
    [string]$BenchExe = ''
)

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $BenchExe) {
    $BenchExe = Join-Path $repo 'build\windows-tests\RelWithDebInfo\hcsbench.exe'
}
if (-not (Test-Path $BenchExe)) {
    Write-Error "hcsbench not found: $BenchExe"
    exit 1
}
$logDir = Join-Path $repo 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$stamp = Get-Date -Format 'yyyyMMdd-HHmmss'
$logPath = Join-Path $logDir "soak-$stamp.log"
$summaryPath = Join-Path $logDir "soak-$stamp.summary.json"

$end = (Get-Date).AddMinutes($Minutes)
$iterations = 0
$failedRows = 0
$failedRuns = 0
$rowsPerRun = 0

Write-Host "SOAK START device=$Device minutes=$Minutes log=$logPath"
while ((Get-Date) -lt $end) {
    $iterations++
    $previous = $ErrorActionPreference
    $ErrorActionPreference = 'Continue' # hcsbench may print benign verify warnings on stderr
    $json = & $BenchExe --device $Device --reps 3 --warmup 1 --json 2>$null | Out-String
    $ErrorActionPreference = $previous
    try {
        $rows = @($json | ConvertFrom-Json)
        $rowsPerRun = $rows.Count
        $bad = @($rows | Where-Object { -not $_.verified }).Count
        $failedRows += $bad
        if ($bad -gt 0 -or $rows.Count -eq 0) { $failedRuns++ }
        Add-Content -Path $logPath -Value ("[{0}] iter={1} rows={2} unverified={3}" -f `
            (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $iterations, $rows.Count, $bad)
    } catch {
        $failedRuns++
        Add-Content -Path $logPath -Value ("[{0}] iter={1} JSON_ERROR {2}" -f `
            (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $iterations, $_.Exception.Message)
    }
    Start-Sleep -Seconds 2
}

$summary = [ordered]@{
    device = $Device
    minutes = $Minutes
    iterations = $iterations
    rows_per_run = $rowsPerRun
    unverified_rows = $failedRows
    failed_runs = $failedRuns
    result = if ($failedRuns -eq 0 -and $failedRows -eq 0 -and $iterations -gt 0) { 'PASS' } else { 'FAIL' }
    log = $logPath
}
$summary | ConvertTo-Json | Set-Content -Path $summaryPath
Write-Host ("SOAK {0}: iterations={1} failed_runs={2} unverified_rows={3}" -f `
    $summary.result, $iterations, $failedRuns, $failedRows)
if ($summary.result -ne 'PASS') { exit 1 }
