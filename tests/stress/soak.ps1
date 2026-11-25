param(
    [int]$Minutes = 60,
    [string]$Exe = "D:\HCSUDA\build\windows-tests\RelWithDebInfo\hcsbench.exe",
    [string]$Log = "D:\HCSUDA\artifacts\soak.log"
)

$ErrorActionPreference = "Stop"
if (-not (Test-Path -LiteralPath $Exe)) { throw "hcsbench not found: $Exe" }
# Use a private copy so a concurrent rebuild cannot replace the binary mid-run.
$private = Join-Path $env:TEMP ("hcsuda-soak-bin-" + [guid]::NewGuid().ToString("N") + ".exe")
Copy-Item -LiteralPath $Exe -Destination $private -Force
$end = (Get-Date).AddMinutes($Minutes)
$iteration = 0
New-Item -ItemType Directory -Force -Path (Split-Path -Parent $Log) | Out-Null
try {
    while ((Get-Date) -lt $end) {
        $iteration++
        $output = & $private --device 0 --reps 5 --warmup 1 --size 65536 --mat 128 2>&1 | Out-String
        $medians = ($output -split "`r?`n" | Where-Object { $_ -match 'median' }) -join ' | '
        $line = "{0:o} iteration={1} {2}" -f (Get-Date), $iteration, $medians
        Add-Content -LiteralPath $Log -Value $line
        Start-Sleep -Seconds 2
    }
    Write-Host "SOAK_COMPLETE iterations=$iteration log=$Log"
} finally {
    Remove-Item -LiteralPath $private -Force -ErrorAction SilentlyContinue
}
