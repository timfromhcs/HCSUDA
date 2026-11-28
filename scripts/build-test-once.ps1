# One Build -> Test cycle for the limitless loop. Uses a lock so manual runs and the
# background loop never mutate the same build tree concurrently.
$ErrorActionPreference = 'Continue'
$repo = Split-Path -Parent $PSScriptRoot
$logDir = Join-Path $repo 'logs'
New-Item -ItemType Directory -Force -Path $logDir | Out-Null
$lockPath = Join-Path $logDir 'limitless-loop.lock'
$logPath = Join-Path $logDir 'limitless-loop.log'

$lock = $null
try {
    $lock = [System.IO.File]::Open($lockPath, 'OpenOrCreate', 'Write', 'None')
} catch {
    Write-Host "BUILD-TEST skipped: another cycle holds the lock."
    exit 0
}

function Write-Log([string]$msg) {
    $line = "[{0}] {1}" -f (Get-Date -Format 'yyyy-MM-dd HH:mm:ss'), $msg
    Add-Content -Path $logPath -Value $line
    Write-Host $line
}

$stamp = Get-Date -Format 'yyyy-MM-dd HH:mm:ss'
Write-Log "CYCLE START ($stamp)"

Push-Location $repo
try {
    $configure = & cmake --preset windows-tests 2>&1
    $cfgCode = $LASTEXITCODE
    Write-Log "configure exit=$cfgCode"
    if ($cfgCode -eq 0) {
        $build = & cmake --build --preset windows-tests --parallel 2>&1
        $buildCode = $LASTEXITCODE
        Write-Log "build exit=$buildCode"
        if ($buildCode -eq 0) {
            $test = & ctest --preset windows-tests --output-on-failure 2>&1
            $testCode = $LASTEXITCODE
            Write-Log "test exit=$testCode"
            Add-Content -Path $logPath -Value ($test | Select-Object -Last 12)
        } else {
            Add-Content -Path $logPath -Value ($build | Select-Object -Last 25)
        }
    } else {
        Add-Content -Path $logPath -Value ($configure | Select-Object -Last 25)
    }
} finally {
    Pop-Location
    if ($lock) { $lock.Dispose(); Remove-Item -Path $lockPath -Force -ErrorAction SilentlyContinue }
}
Write-Log "CYCLE END"
