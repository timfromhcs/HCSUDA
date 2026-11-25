param([string]$Source = "D:\HCSUDA\installer")

$ErrorActionPreference = "Stop"
$testRoot = Join-Path $env:TEMP ("hcsuda-install-test-" + [guid]::NewGuid().ToString("N"))
$installDir = Join-Path $testRoot "Programs\HCSUDA"
New-Item -ItemType Directory -Force -Path $testRoot | Out-Null

try {
    # Replicate the extracted package layout: scripts and binaries in one directory.
    $package = Join-Path $testRoot "package"
    New-Item -ItemType Directory -Force -Path $package | Out-Null
    Copy-Item -LiteralPath (Join-Path $Source "install.ps1") -Destination $package -Force
    Copy-Item -LiteralPath (Join-Path $Source "uninstall.ps1") -Destination $package -Force
    $buildDir = "D:\HCSUDA\build\windows-tests\RelWithDebInfo"
    foreach ($binary in @("hcsdoctor.exe", "hcsbench.exe", "hcscc.exe", "hcsport.exe",
                          "hcsbridge-mcp.exe", "hcsbridge-rest.exe")) {
        Copy-Item -LiteralPath (Join-Path $buildDir $binary) -Destination $package -Force
    }
    foreach ($doc in @("LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md", "README.md")) {
        Copy-Item -LiteralPath (Join-Path "D:\HCSUDA" $doc) -Destination $package -Force
    }
    & (Join-Path $package "install.ps1") -InstallDir $installDir -Quiet
    $expected = @("hcsdoctor.exe", "hcsbench.exe", "hcscc.exe", "hcsport.exe",
                  "hcsbridge-mcp.exe", "hcsbridge-rest.exe",
                  "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md", "README.md", "uninstall.ps1")
    foreach ($item in $expected) {
        if (-not (Test-Path -LiteralPath (Join-Path $installDir $item))) {
            throw "installer did not place $item"
        }
    }

    # The installer must never remove the system Vulkan loader or GPU driver files.
    $loader = Join-Path $env:WINDIR "System32\vulkan-1.dll"
    if (-not (Test-Path -LiteralPath $loader)) { throw "system Vulkan loader is missing" }

    $doctor = Join-Path $installDir "hcsdoctor.exe"
    $report = & $doctor --json 2>$null | Out-String
    if ($report -notmatch '"product":"HCSUDA"') { throw "installed hcsdoctor did not run" }

    & (Join-Path $installDir "uninstall.ps1") -InstallDir $installDir
    if (Test-Path -LiteralPath (Join-Path $installDir "hcsdoctor.exe")) {
        throw "uninstall left binaries behind"
    }
    if (-not (Test-Path -LiteralPath $loader)) { throw "uninstall removed the Vulkan loader" }
    Write-Host "INSTALLER_TEST_PASS"
} finally {
    if (Test-Path -LiteralPath $testRoot) {
        Remove-Item -LiteralPath $testRoot -Recurse -Force -ErrorAction SilentlyContinue
    }
}
