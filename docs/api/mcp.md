# HCSBridge MCP — implemented stdio protocol (Wave 9, first slice)

`hcsbridge-mcp.exe` is a local MCP server using stdio transport. It writes **only** JSON-RPC
messages to stdout; diagnostics go to stderr. No HTTP listener is started.

## Implemented messages

| Method | Behaviour |
|---|---|
| `initialize` | negotiates protocol version `2025-11-25`; advertises `tools` capability only |
| `notifications/initialized` | accepted, no reply (correct notification semantics) |
| `tools/list` | five read-only tools |
| `tools/call` | dispatches a named tool or returns JSON-RPC `-32601` |
| `resources/list`, `prompts/list` | empty lists |
| anything else | `-32601 Method not found` |

Malformed, empty, oversized (>1 MiB) and deeply nested (>32 levels) messages are rejected with
`-32600` before any tool dispatch.

## Tools (safe set)

* `hcsuda_status` — product/version/API version, device count.
* `hcsuda_list_devices` — name, backend, kind, vendor and capability bitmask per device.
* `hcsuda_get_memory_budget` — driver-reported heap/budget/usage with an explicit "not a VRAM
  size" note.
* `hcsuda_get_diagnostics` — sanitized version/capability report.
* `hcsuda_list_models` — safe metadata of the indexed local model catalog (id, name, format,
  size, hash flag, `runnable:false`); no paths, file contents or downloads.

There is deliberately **no** `run_command`, arbitrary file read, download, DLL load or arbitrary
kernel compilation tool. Tool arguments are not used to open files or make network requests.

## Limits

The in-process handler `hcs::mcp::handle(line)` processes exactly one message and is unit-tested
without spawning a process. The executable owns runtime init/shutdown and relays one line at a
time. Streamable HTTP transport is **not implemented**; the local REST API is a separate wave.

## Evidence

`tests/unit/test_mcp.cpp`: initialization, notification framing, safe tool inventory, real
runtime data from tool calls, unknown method/tool errors, and malformed/oversized/deep input.
Manual end-to-end check:

```powershell
'{"jsonrpc":"2.0","id":1,"method":"initialize"}' | .\hcsbridge-mcp.exe
```
