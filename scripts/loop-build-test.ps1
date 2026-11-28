# Never-ending Build -> Test -> Fix loop driver.
# Stops only when logs/limitless-loop.stop exists. Each cycle is logged to
# logs/limitless-loop.log and never blocks another cycle via the lock file.
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$stopFile = Join-Path $repo 'logs\limitless-loop.stop'
$once = Join-Path $PSScriptRoot 'build-test-once.ps1'
$logPath = Join-Path $repo 'logs\limitless-loop.log'
New-Item -ItemType Directory -Force -Path (Split-Path $logPath) | Out-Null

Add-Content -Path $logPath -Value ("[{0}] LOOP STARTED" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
while (-not (Test-Path $stopFile)) {
    & $once | Out-Null
    for ($i = 0; $i -lt 6 -and -not (Test-Path $stopFile); $i++) {
        Start-Sleep -Seconds 10
    }
}
Add-Content -Path $logPath -Value ("[{0}] LOOP STOPPED" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'))
