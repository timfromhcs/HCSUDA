# V1 release checklist (evidence-based, 2026-11-28)

Every checked item links to real evidence in `docs/TEST_MATRIX.md`, `docs/BUILD_LOG.md` or
`docs/AGENT_STATUS.md`. Unchecked items are external/owner actions, never silently passed.

## Runtime and compute

- [x] Launches on Windows 11 x64 without developer tools — INS-01/02, installer EXE.
- [x] CPU fallback without Vulkan — `windows-cpu-only` profile, 93/93 tests.
- [x] Vulkan devices discovered and selected by capability — API-03, VK-01, `hcsdoctor`.
- [x] Device/driver/API/memory report is truthful — VK-01, no invented VRAM/utilization/temperature.
- [x] Memory cap and graceful OOM — VK-05.
- [x] Kernels correct on Vulkan and CPU reference — CPU-01..06, VK-02..04, KC-01..13.
- [x] Flow/event/graph deterministic tests — FL-01..05, GR-01..04, STR-01..02.
- [x] No false FP16/BF16/matrix/subgroup claims — capability bits + KNOWN_LIMITATIONS.
- [x] Pipeline cache survives restart and rejects corruption — VK-06, `hcsbench --cache-dir`.
- [x] Validation layers clean — VAL-01, 96/96, zero errors.

## Translation

- [x] Migration report distinguishes translated / manual / unsupported — PORT-01..03, TR-01..03.
- [x] Advertised source construct covered by conformance test — TR-01/02 (vecadd subset).
- [x] Unsupported constructs fail closed — TR-03, PORT-02.
- [x] PTX/CUBIN/SASS marked unsupported — `docs/compatibility/cuda-api-matrix.md`, v1-scope.
- [x] No global CUDA DLL or driver replacement — installer tests INS-01..04.

## Models and AI

- [x] User-selectable HDD model path, catalog, SHA-256, path safety — MV-01..04, SET-01..05.
- [x] HDD described as storage, not execution memory — UI note + KNOWN_LIMITATIONS.
- [x] Safe metadata exposed through REST and MCP — REST-07, MCP-07, no paths/file contents.
- [ ] GGUF inference adapter with tested quantisation — **out of V1 scope** by ADR/scope decision;
      every model reports `runnable: false`. Not a pending bug, an explicit scope reduction.

## UI / API / MCP

- [x] Every visible navigation section works — UI-01..05, SET-01..05 (Dashboard, Devices,
      Memory, Models, Settings, About, Compatibility).
- [x] Settings persist, validate and reset — SET-01..05.
- [x] Visualizer shows only queried data or "Not exposed" — UI-01, Dashboard screenshot.
- [x] REST local-only, authenticated, documented — REST-01..07, `docs/api/openapi.json`, API-08.
- [x] MCP stdio protocol-tested, five read-only tools — MCP-01..07.
- [x] No shell/arbitrary file/network tool — MCP-03, REST-03, code review.

## Installer and release

- [x] Single `HCSUDA-Setup-x64.exe` produced — `installer/`, INS-01/02.
- [x] Clean-ish staged install, uninstall, in-place and cross-version upgrade — INS-01..04.
- [x] No driver/loader/registry modification — INS-01, uninstall tests.
- [x] Third-party notices, SBOM, license files — `THIRD_PARTY_NOTICES.md`, `NOTICE`, SBOM artifact.
- [x] EXE scanned, checksum published — SEC-01 (132 tracked files), installer docs. Rebuilt after
      the latest changes; SHA-256 `F18D065014ED7535FBA03914720543D8E24E2AD152F8AF62461FE373A249F6E0`.
- [x] Hosted CI green — GitHub Actions run 46155822668, all seven jobs success incl. artifact scan (CI-01).
- [x] README and compatibility matrix match the build — README compatibility matrix, this file.
- [ ] Code signing with a publisher certificate — **no certificate configured**; installer is
      honestly unsigned and published with SHA-256.
- [ ] GitHub Release/tag `v0.1.0-rc.1` — owner approval required; see `docs/release/publication.md`.

## Hardware QA

- [x] Radeon 680M reference run recorded — VK-01..06, benchmark baseline, TEST_MATRIX header.
- [ ] Additional Intel/NVIDIA Vulkan device — **not verified**, no such hardware on this machine.
      Cross-vendor support is described as capability-dependent, not universal.

## Stability gates (re-run after every code change)

- [x] `cmake --build --preset windows-tests --parallel` + `ctest --preset windows-tests` — green.
- [x] `ctest --preset windows-cpu-only` — 93/93; `ctest --preset windows-validation` — 96/96.
- [x] `cmake --build --preset windows-analyze` — zero warnings.
- [x] `scripts/check-secrets.ps1` — PASS (132 files); `scripts/check-openapi.ps1` — PASS.
- [x] Clean out-of-tree build from an empty directory — CV-01, 9 shaders compiled fresh, 96/96.
- [x] `scripts/check-artifact.ps1` — ART-01, checksum match, no secret patterns or forbidden imports.
- [x] `scripts/soak.ps1 -Minutes 60` — PASS, 1418 iterations, 0 failed runs, 0 unverified rows
      (SOAK-02; 1-minute smoke also passed with 24 iterations).

No P0/P1 bug and no known critical/high security issue is open in the implemented scope.
Exact owner actions for the unchecked external items: `docs/release/EXTERNAL_BLOCKERS.md`.

Made with love by HCSMedia & timfromhcs.
