param(
    [string]$InstallDir = "",
    [switch]$Quiet
)

$ErrorActionPreference = "Stop"
if ([string]::IsNullOrWhiteSpace($InstallDir)) {
    $InstallDir = Join-Path $env:LOCALAPPDATA "Programs\HCSUDA"
}

# User-mode install only. Never touch System32, DriverStore, Vulkan ICD registry keys,
# nvcuda.dll, or the graphics driver. The existing vendor driver remains authoritative.
$source = Split-Path -Parent $MyInvocation.MyCommand.Path
New-Item -ItemType Directory -Force -Path $InstallDir | Out-Null

$items = @(
    "hcsdoctor.exe", "hcsbench.exe", "hcscc.exe", "hcsport.exe",
    "hcsbridge-mcp.exe", "hcsbridge-rest.exe",
    "LICENSE", "NOTICE", "THIRD_PARTY_NOTICES.md", "README.md"
)
foreach ($item in $items) {
    $candidate = Join-Path $source $item
    if (-not (Test-Path $candidate)) {
        $candidate = Join-Path (Join-Path $source "bin") $item
    }
    if (Test-Path $candidate) {
        Copy-Item -LiteralPath $candidate -Destination $InstallDir -Force
    } elseif (-not $Quiet) {
        Write-Warning "missing install item: $item"
    }
}

Copy-Item -LiteralPath (Join-Path $source "uninstall.ps1") -Destination $InstallDir -Force

# Shortcut is optional and user-scoped; Start Menu only, no desktop shortcut by default.
try {
    $startMenu = Join-Path $env:APPDATA "Microsoft\Windows\Start Menu\Programs"
    $wsh = New-Object -ComObject WScript.Shell
    $shortcut = $wsh.CreateShortcut((Join-Path $startMenu "HCSUDA Doctor.lnk"))
    $shortcut.TargetPath = Join-Path $InstallDir "hcsdoctor.exe"
    $shortcut.WorkingDirectory = $InstallDir
    $shortcut.Description = "HCSUDA device and capability report"
    $shortcut.Save()
} catch {
    if (-not $Quiet) { Write-Warning "could not create Start Menu shortcut: $_" }
}

$config = Join-Path $env:LOCALAPPDATA "HCSUDA"
$logs = Join-Path $config "Logs"
$models = Join-Path $config "Models"
foreach ($dir in @($config, $logs, $models)) {
    New-Item -ItemType Directory -Force -Path $dir | Out-Null
}

if (-not $Quiet) {
    Write-Host "HCSUDA installed to $InstallDir"
    Write-Host "Model directory: $models (HDD path can be selected in configuration)"
    Write-Host "No GPU driver, Vulkan loader or ICD was modified."
}
