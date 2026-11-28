$ErrorActionPreference = 'Stop'
$root = Split-Path -Parent $MyInvocation.MyCommand.Path
Push-Location $root
try {
    & npx --yes -p wabt wat2wasm kernel.wat -o kernel.wasm
    if (-not (Test-Path (Join-Path $root 'kernel.wasm'))) { throw 'WASM build produced no file' }
    $size = (Get-Item (Join-Path $root 'kernel.wasm')).Length
    Write-Host "WASM_BUILD_PASS bytes=$size"
} finally { Pop-Location }
