# Validates the checked-in OpenAPI 3.1 contract and the endpoints implemented in V1.
param([string]$Document = '')

$ErrorActionPreference = 'Stop'
$repo = Split-Path -Parent $PSScriptRoot
if (-not $Document) { $Document = Join-Path $repo 'docs\api\openapi.json' }
if (-not (Test-Path $Document)) { throw "OpenAPI document not found: $Document" }

$spec = Get-Content -LiteralPath $Document -Raw | ConvertFrom-Json
if ($spec.openapi -ne '3.1.0') { throw "expected OpenAPI 3.1.0, got $($spec.openapi)" }
if ($spec.info.version -ne '0.1.0') { throw "expected API version 0.1.0, got $($spec.info.version)" }

$required = @(
    '/api/v1/health', '/api/v1/version', '/api/v1/devices',
    '/api/v1/memory/budget', '/api/v1/runtime/status', '/api/v1/models',
    '/api/v1/settings', '/api/v1/settings/reset'
)
$paths = $spec.paths.PSObject.Properties.Name
foreach ($path in $required) {
    if ($paths -notcontains $path) { throw "missing path: $path" }
}
$description = [string]$spec.info.description
if ($description -notmatch '429' -or $description -notmatch 'rate') {
    throw 'rate limiting is not documented'
}
Write-Host ("OPENAPI_PASS ({0} paths, {1} required endpoints)" -f $paths.Count, $required.Count)
