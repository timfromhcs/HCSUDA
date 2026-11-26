param(
    [string]$RepoRoot = (Resolve-Path (Join-Path $PSScriptRoot "..")).Path,
    [string]$BuildDir = (Join-Path $RepoRoot "build\windows-tests\RelWithDebInfo")
)

$ErrorActionPreference = "Stop"
$artifacts = Join-Path $RepoRoot "artifacts"
$built = Join-Path $BuildDir "HCSUDA-Setup-x64.exe"
$target = Join-Path $artifacts "HCSUDA-Setup-x64.exe"
if (-not (Test-Path $built)) {
    throw "native setup executable not found: $built (build the windows-tests preset first)"
}
New-Item -ItemType Directory -Force -Path $artifacts | Out-Null
Copy-Item -LiteralPath $built -Destination $target -Force
$hash = Get-FileHash -LiteralPath $target -Algorithm SHA256
Set-Content -LiteralPath (Join-Path $artifacts "HCSUDA-Setup-x64.exe.sha256") `
            -Value "$($hash.Hash)  HCSUDA-Setup-x64.exe"
Write-Host "Created $target"
Write-Host "SHA-256 $($hash.Hash)"
Write-Host "The installer is unsigned; the checksum above is the verification mechanism."
