# Security policy

## Supported versions

HCSUDA is pre-release. Only the latest commit on `main` receives security fixes.

## Reporting a vulnerability

Do not open a public issue for a vulnerability. Report it privately to the repository owner
(HCSMedia / timfromhcs) with:

* affected version or commit,
* reproduction steps,
* observed and expected behaviour,
* whether the issue is reachable from an API, MCP client, model file, or shader input.

You will receive an acknowledgement and a remediation plan. Please allow time for a fix
before public disclosure.

## Security model (current build)

* **No network activity.** The runtime performs no downloads, telemetry, or update checks.
* **No driver modification.** HCSUDA is user-mode only. It does not write to `System32`,
  `DriverStore`, Vulkan ICD registry keys, or NVIDIA/AMD driver directories, and never
  replaces `vulkan-1.dll`.
* **No arbitrary code execution surface.** There is no REST or MCP server in 0.1.0. When
  added, they must bind to localhost by default, require a random per-install token compared
  in constant time, validate `Origin`, limit request body size and JSON depth, and expose no
  shell, DLL-load, or unrestricted filesystem tool.
* **Untrusted input.** Future parsers (HCSIR, PTX, CUDA source) must bound input size, token
  count, nesting depth, instruction count and compile time. They must not read files, spawn
  processes, or access the network.
* **Memory safety.** No allocation may exceed the configured cap. OOM returns
  `HCS_E_OUT_OF_MEMORY`; the runtime never attempts to exhaust UMA memory.
* **Logs.** Diagnostics must not contain secrets or unredacted user paths by default.
* **Secrets in Git.** Never commit tokens, keys, model weights, dumps, or personal paths.
  `.gitignore` covers the known patterns; review `git diff --cached` before every commit.

## Hardening backlog

| Item | Status |
|---|---|
| `/guard:cf`, ASLR, DEP in release builds | pending (installer wave) |
| Fuzzing for HCSIR/PTX parsers | pending (parsers do not exist yet) |
| Dependency and secret scanning in CI | workflow drafted, not enforced |
| SBOM generation | pending |
| Code signing | pending (no certificate configured) |
