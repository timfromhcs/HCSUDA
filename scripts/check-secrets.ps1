# Lightweight secret scan over tracked files. Exits non-zero on a suspected credential.
# Documentation and test fixtures contain explicit dummy tokens and are excluded by design.
param([string]$RepoRoot = "D:\HCSUDA")

$ErrorActionPreference = "Stop"
Push-Location $RepoRoot
try {
    $files = git ls-files
    $patterns = @(
        'ghp_[A-Za-z0-9]{20,}',
        'github_pat_[A-Za-z0-9_]{20,}',
        '-----BEGIN (RSA|EC|OPENSSH|DSA)? ?PRIVATE KEY-----',
        'AKIA[0-9A-Z]{16}',
        'sk-[A-Za-z0-9]{32,}'
    )
    $hits = @()
    foreach ($file in $files) {
        if ($file -match '^(docs/|tests/|artifacts/|build/)') { continue }
        if ($file -match '\.(png|jpg|zip|exe|spv|7z)$') { continue }
        $path = Join-Path $RepoRoot $file
        if (-not (Test-Path -LiteralPath $path)) { continue }
        $text = Get-Content -LiteralPath $path -Raw -ErrorAction SilentlyContinue
        if (-not $text) { continue }
        foreach ($pattern in $patterns) {
            if ($text -match $pattern) { $hits += "$file : $pattern" }
        }
    }
    if ($hits.Count -gt 0) {
        $hits | ForEach-Object { Write-Error $_ }
        exit 1
    }
    Write-Host "SECRET_SCAN_PASS ($($files.Count) tracked files)"
} finally {
    Pop-Location
}
