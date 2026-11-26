param(
    [Parameter(Mandatory = $true)][string]$OldSetup,
    [Parameter(Mandatory = $true)][string]$NewSetup
)

$ErrorActionPreference = "Stop"
$installDir = Join-Path $env:LOCALAPPDATA "Programs\HCSUDA"

function Invoke-Setup([string]$path) {
    if (-not (Test-Path -LiteralPath $path)) { throw "setup not found: $path" }
    $p = Start-Process -FilePath $path -PassThru
    if (-not $p.WaitForExit(180000)) {
        $p.Kill()
        throw "setup timed out: $path"
    }
    if ($p.ExitCode -ne 0) { throw "setup failed with $($p.ExitCode): $path" }
}

try {
    if (Test-Path -LiteralPath $installDir) {
        # Do not destroy unrelated files: require an explicitly prepared test directory.
        throw "refusing to run: $installDir already exists"
    }
    Invoke-Setup $OldSetup
    if (-not (Test-Path -LiteralPath (Join-Path $installDir "hcsdoctor.exe"))) {
        throw "old setup did not install binaries"
    }
    Set-Content -LiteralPath (Join-Path $installDir "user-sentinel.txt") -Value "keep-me"

    Invoke-Setup $NewSetup
    if (-not (Test-Path -LiteralPath (Join-Path $installDir "user-sentinel.txt"))) {
        throw "cross-version upgrade removed user files"
    }
    $doctor = Join-Path $installDir "hcsdoctor.exe"
    $report = & $doctor --json 2>$null | Out-String
    if ($report -notmatch '"product":"HCSUDA"') { throw "upgraded binary does not run" }
    Write-Host "CROSS_VERSION_UPGRADE_PASS old=$OldSetup new=$NewSetup"
} finally {
    if (Test-Path -LiteralPath (Join-Path $installDir "uninstall.ps1")) {
        & (Join-Path $installDir "uninstall.ps1") -InstallDir $installDir
    }
}
