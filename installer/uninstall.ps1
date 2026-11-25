param(
    [string]$InstallDir = "",
    [switch]$RemoveData
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\HCSUDA"
}

# Remove only files installed by HCSUDA. User models, caches and config are preserved unless
# -RemoveData is explicitly supplied. No system driver, DLL, PATH entry or global registry key.
$files = @(
    "hcsdoctor.exe", "hcsbench.exe", "hcscc.exe", "hcsport.exe",
    "hcsbridge-mcp.exe", "hcsbridge-rest.exe",
    "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md", "README.md", "uninstall.ps1"
)
foreach ($file in $files) {
    $path = Join-Path $InstallDir $file
    if (Test-Path -LiteralPath $path) {
        Remove-Item -LiteralPath $path -Force
    }
}

try {
    $shortcut = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs\HCSUDA Doctor.lnk"
    if (Test-Path -LiteralPath $shortcut) {
        Remove-Item -LiteralPath $shortcut -Force
    }
} catch { }

if ((Test-Path -LiteralPath $InstallDir) -and -not (Get-ChildItem -LiteralPath $InstallDir -Force)) {
    Remove-Item -LiteralPath $InstallDir -Force
}

if ($RemoveData) {
    $config = Join-Path $env:LOCALAPPDATA "HCSUDA"
    if (Test-Path -LiteralPath $config) {
        Remove-Item -LiteralPath $config -Recurse -Force
    }
}

Write-Host "HCSUDA uninstalled from $InstallDir (user data preserved: $(-not $RemoveData))."
