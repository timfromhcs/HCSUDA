# HCSBridge REST — implemented contract (Wave 9)

`hcsbridge-rest.exe` is a **localhost-only** HTTP/1.1 service. It binds `127.0.0.1` with no
option to bind another interface. Per process it generates a random 64-hex bearer token and
prints it to stderr. No token is hardcoded or stored in the repository.

## Endpoints

| Method | Path | Auth | Result |
|---|---|---|---|
| GET | `/api/v1/health` | none | `{"status":"ok","local_only":true}` |
| GET | `/api/v1/version` | bearer | product, version, C ABI version |
| GET | `/api/v1/devices` | bearer | index, name, backend, kind per real device |
| GET | `/api/v1/runtime/status` | bearer | same device inventory |
| GET | `/api/v1/memory/budget` | bearer | driver-reported heap/budget/usage |
| anything else | — | — | 404 JSON |

There is **no** endpoint for arbitrary code execution, shell commands, DLL loading, model
download or unrestricted file access. `POST /api/v1/jobs/compute` is intentionally absent.

## Security controls implemented

* Bind to `INADDR_LOOPBACK` only.
* Constant-time bearer-token comparison; missing/wrong token → 401.
* `Origin` header accepted only from `localhost`/`127.0.0.1`/`[::1]` (any port) → otherwise 403.
* Request body limited to 64 KiB → otherwise 413.
* Target path rejects `..` and backslashes → 400.
* `X-Content-Type-Options: nosniff`, `Cache-Control: no-store`, `Connection: close`.
* Errors are structured JSON with `status`, `code` and `message`.

The server reads at most `kMaxRequestBytes + 1` bytes per connection, so a hostile client cannot
make it buffer unbounded data. TLS is not used: the service is local-only by design.

## Usage

```powershell
.\hcsbridge-rest.exe --port 8710
# stderr: "bearer token: <64 hex chars>"

Invoke-RestMethod http://127.0.0.1:8710/api/v1/health
Invoke-RestMethod http://127.0.0.1:8710/api/v1/devices `
  -Headers @{ Authorization = "Bearer <token>" }
```

## Not implemented

* OpenAPI 3.1 document generation (the table above is the current hand-written contract).
* Model endpoints, benchmark endpoints, translation endpoints, authentication token rotation.
* Streamable HTTP MCP / remote binding / TLS.
* Rate limiting beyond the bounded request/body size.

## Evidence

`tests/unit/test_rest.cpp` covers health, token generation and comparison, auth failures,
cross-origin rejection, oversized bodies, path traversal, unknown routes and real device/budget
data. A live loopback check returned `health=200`, `unauthorized=401`, `devices=200` with real
Radeon 680M and CPU records.
