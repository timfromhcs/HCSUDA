param([string]$Source = "D:\HCSUDA\installer")

$ErrorActionPreference = "Stop"
$testRoot = Join-Path $env:TEMP ("hcsuda-upgrade-test-" + [guid]::NewGuid().ToString("N"))
$installDir = Join-Path $testRoot "Programs\HCSUDA"
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

try {
    $package = Join-Path $testRoot "package"
    New-Item -ItemType Directory -Force -Path $package | Out-Null
    foreach ($script in @("install.ps1", "uninstall.ps1")) {
        Copy-Item -LiteralPath (Join-Path $Source $script) -Destination $package -Force
    }
    $buildDir = "D:\HCSUDA\build\windows-tests\RelWithDebInfo"
    foreach ($binary in @("hcsdoctor.exe", "hcsbench.exe", "hcscc.exe", "hcsport.exe",
                          "hcsbridge-mcp.exe", "hcsbridge-rest.exe")) {
        Copy-Item -LiteralPath (Join-Path $buildDir $binary) -Destination $package -Force
    }
    foreach ($doc in @("LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md", "README.md")) {
        Copy-Item -LiteralPath (Join-Path "D:\HCSUDA" $doc) -Destination $package -Force
    }

    # First install (simulating the previous build), then an in-place reinstall.
    & (Join-Path $package "install.ps1") -InstallDir $installDir -Quiet
    Set-Content -LiteralPath (Join-Path $installDir "user-sentinel.txt") -Value "keep"
    & (Join-Path $package "install.ps1") -InstallDir $installDir -Quiet
    if (-not (Test-Path -LiteralPath (Join-Path $installDir "user-sentinel.txt"))) {
        throw "reinstall removed user data in the install directory"
    }
    $doctor = Join-Path $installDir "hcsdoctor.exe"
    $report = & $doctor --json 2>$null | Out-String
    if ($report -notmatch '"product":"HCSUDA"') { throw "reinstalled binary does not run" }

    & (Join-Path $installDir "uninstall.ps1") -InstallDir $installDir
    if (Test-Path -LiteralPath (Join-Path $installDir "hcsdoctor.exe")) {
        throw "uninstall after upgrade left binaries behind"
    }
    $loader = Join-Path $env:WINDIR "System32\vulkan-1.dll"
    if (-not (Test-Path -LiteralPath $loader)) { throw "system Vulkan loader was removed" }
    Write-Host "UPGRADE_TEST_PASS"
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
