# Automated UI evidence test: starts the local REST service, captures the real
# console with headless Chrome/Edge and validates that the PNG is non-empty.
# Skips with exit 0 when no browser is installed (hosted CI without a browser).
param(
    [Parameter(Mandatory = $true)][string]$ServerExe,
    [string]$SaveTo = ''
)

$ErrorActionPreference = 'Stop'

$browsers = @(
    'C:\Program Files\Google\Chrome\Application\chrome.exe',
    'C:\Program Files (x86)\Google\Chrome\Application\chrome.exe',
    'C:\Program Files (x86)\Microsoft\Edge\Application\msedge.exe',
    'C:\Program Files\Microsoft\Edge\Application\msedge.exe'
)
$browser = $browsers | Where-Object { Test-Path $_ } | Select-Object -First 1
if (-not $browser) {
    Write-Host 'SKIP: no Chrome/Edge installation found; UI capture not available.'
    exit 0
}
if (-not (Test-Path $ServerExe)) {
    Write-Error "REST server binary not found: $ServerExe"
    exit 1
}

# Throwaway token for a local-only test process; never a real credential.
$token = '0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef'
$temp = Join-Path $env:TEMP ('hcsui-' + [guid]::NewGuid().ToString('N'))
New-Item -ItemType Directory -Force -Path $temp | Out-Null

# Pick a free loopback port so parallel CTest runs never collide.
$probe = [System.Net.Sockets.TcpListener]::new([System.Net.IPAddress]::Loopback, 0)
$probe.Start()
$port = $probe.LocalEndpoint.Port
$probe.Stop()

$stdout = Join-Path $temp 'server.out'
$stderr = Join-Path $temp 'server.err'
$server = $null
try {
    $server = Start-Process -FilePath $ServerExe `
        -ArgumentList @('--port', "$port", '--token', $token) `
        -PassThru -WindowStyle Hidden -RedirectStandardOutput $stdout -RedirectStandardError $stderr

    $ready = $false
    for ($i = 0; $i -lt 50; $i++) {
        try {
            $client = [System.Net.Sockets.TcpClient]::new()
            $client.Connect('127.0.0.1', $port)
            $client.Close()
            $ready = $true
            break
        } catch {
            Start-Sleep -Milliseconds 200
        }
    }
    if (-not $ready) {
        throw "REST server did not listen on 127.0.0.1:$port"
    }

    $shot = Join-Path $temp 'ui.png'
    $profile = Join-Path $temp 'profile'
    # Start-Process with redirected streams avoids PowerShell 5.1 treating benign
    # Chrome stderr lines (e.g. external-extension registry warnings) as errors.
    $browserArgs = @(
        '--headless=new', '--disable-gpu', '--hide-scrollbars', '--no-first-run',
        '--no-default-browser-check', '--window-size=1280,900', '--virtual-time-budget=3000',
        ('--user-data-dir="' + $profile + '"'),
        ('--screenshot="' + $shot + '"'),
        ('"http://127.0.0.1:' + $port + '/ui/"')
    )
    $browserOut = Join-Path $temp 'browser.out'
    $browserErr = Join-Path $temp 'browser.err'
    $browserProc = Start-Process -FilePath $browser -ArgumentList $browserArgs -Wait -PassThru `
        -WindowStyle Hidden -RedirectStandardOutput $browserOut -RedirectStandardError $browserErr

    if (-not (Test-Path $shot)) {
        throw "browser did not produce a screenshot (exit $($browserProc.ExitCode))"
    }
    $bytes = [System.IO.File]::ReadAllBytes($shot)
    if ($bytes.Length -lt 10000) { throw "screenshot too small: $($bytes.Length) bytes" }
    if ($bytes[0] -ne 0x89 -or $bytes[1] -ne 0x50 -or $bytes[2] -ne 0x4E -or $bytes[3] -ne 0x47) {
        throw 'screenshot is not a PNG'
    }
    if ($SaveTo) {
        Copy-Item -Path $shot -Destination $SaveTo -Force
    }
    Write-Host "PASS: live UI screenshot captured ($($bytes.Length) bytes) from port $port"
    exit 0
} catch {
    Write-Error "FAIL: $($_.Exception.Message)"
    exit 1
} finally {
    if ($server -and -not $server.HasExited) {
        Stop-Process -Id $server.Id -Force -ErrorAction SilentlyContinue
        try { Wait-Process -Id $server.Id -Timeout 10 -ErrorAction SilentlyContinue } catch { }
    }
    # A stale server holding the EXE would break the next build (LNK1168).
    Get-Process -Name 'hcsbridge-rest' -ErrorAction SilentlyContinue |
        Where-Object { $_.Path -eq $ServerExe } |
        ForEach-Object { Stop-Process -Id $_.Id -Force -ErrorAction SilentlyContinue }
    Remove-Item -Path $temp -Recurse -Force -ErrorAction SilentlyContinue
}
