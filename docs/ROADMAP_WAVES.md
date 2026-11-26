# Wave roadmap and acceptance state

This file is the machine-readable-ish companion to `docs/AGENT_STATUS.md`. Each wave ends
with a build, a test run, and an updated TEST_MATRIX entry.

| Wave | Gate | State |
|---|---|---|
| 0 | Clean configure/build, project contract | **GREEN** |
| 1 | CPU-only sample passes; ABI and error tests | **GREEN** |
| 2 | Accurate device report on real hardware; CPU fallback | **GREEN** |
| 3 | Vulkan vector-add (and more) passes vs CPU reference | **GREEN** |
| 4 | HCSIR v1 parser/verifier/interpreter + GLSL lowering + GPU execution | **GREEN** — the same HCSIR vecadd module passes on the CPU interpreter and on the Vulkan GPU |
| 5 | Async flows/events/graphs; race-free under stress | **GREEN** — graph replay on both backends; 500-submission GPU stress and validation profile zero errors |
| 6 | Reduction/scan/matmul suite + benchmark data | **GREEN** for the defined set: reduce, matmul, scan, transpose verified on CPU+Vulkan; benchmark rows recorded. Histogram/image primitives are not in V1 scope. |
| 7 | CUDA source subset migration with report | **GREEN for V1 scope** — scanner + one conformance-tested vecadd translation; PTX/CUBIN explicitly out of V1 |
| 8 | GGUF model through verified adapter | **Scope reduced** — catalog/checksum/path safety tested; inference deliberately omitted from V1 claims |
| 9 | REST + MCP local-only, authenticated, tested | **GREEN for V1 scope** — MCP, REST with token auth, OpenAPI 3.1 and settings endpoints |
| 10 | UI pages, settings persistence, no dead controls | **GREEN for V1 scope** — live data UI + persisted settings; full navigation out of V1 scope |
| 11 | Single EXE installer, clean-VM install/uninstall | **GREEN on reference machine** — native EXE, install/uninstall + same-version and cross-version upgrade (INS-01..04); clean-VM still unverified |
| 12 | Stress/soak/fuzz/static analysis; no critical issues | **PARTIAL** — stress/fuzz, 60-min soak (1610 iterations, 0 failures), secret scan pass; static analysis tooling not added |
| 13 | README/matrix match build; release candidate | **PARTIAL** — OpenAPI/SBOM/scope docs, public repo pushed, hosted CI green (CI-01); Release + signing pending |

## Next concrete task

Only external blockers remain: clean-VM test, code signing, a manually approved GitHub
Release, and cross-vendor hardware verification. Cross-version upgrade is verified on the
reference machine (INS-04). No local wave work is open.

Deferred (not release blockers): HCSIR serialization/round-trip caching, static analysis tooling.

## Definition of done for any wave

* `cmake --build --preset windows-tests` succeeds with no new warnings in project code.
* `ctest --preset windows-tests` passes.
* `docs/TEST_MATRIX.md`, `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md` updated.
* No claim in README exceeds what the tests prove.
