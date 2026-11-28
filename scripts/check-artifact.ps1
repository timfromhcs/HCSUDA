# Release artifact gate: verify SHA-256 and scan the installer for obvious secrets and
# forbidden system imports. No signature is claimed; unsigned status is documented separately.
param([string]$Exe = '')

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Exe) { $Exe = Join-Path $repo 'artifacts\HCSUDA-Setup-x64.exe' }
if (-not (Test-Path $Exe)) { throw "artifact not found: $Exe" }

$sha = (Get-FileHash -LiteralPath $Exe -Algorithm SHA256).Hash.ToLowerInvariant()
$checksumFile = "$Exe.sha256"
if (Test-Path $checksumFile) {
    $expected = ((Get-Content -LiteralPath $checksumFile -TotalCount 1) -split '\s+')[0].ToLowerInvariant()
    if ($sha -ne $expected) { throw "SHA-256 mismatch: actual $sha expected $expected" }
}

$bytes = [System.IO.File]::ReadAllBytes($Exe)
$text = [System.Text.Encoding]::ASCII.GetString($bytes)
$forbidden = @(
    'BEGIN PRIVATE KEY', 'BEGIN RSA PRIVATE KEY', 'AKIA[0-9A-Z]{16}',
    'ghp_[A-Za-z0-9]{30,}', 'github_pat_', 'xoxb-'
)
foreach ($pattern in $forbidden) {
    if ($text -match $pattern) { throw "secret-like pattern in artifact: $pattern" }
}

$dump = Get-Command dumpbin -ErrorAction SilentlyContinue
if ($dump) {
    $imports = (& $dump.Source /dependents $Exe | Out-String).ToLowerInvariant()
    foreach ($dll in @('nvcuda.dll', 'nvrtc.dll', 'cudart64', 'vulkan-1.dll')) {
        if ($imports -match [regex]::Escape($dll)) {
            throw "forbidden static import in installer: $dll"
        }
    }
}

Write-Host ("ARTIFACT_PASS sha256={0} bytes={1} imports_checked={2}" -f `
    $sha.Substring(0, 16), $bytes.Length, [bool]$dump)
