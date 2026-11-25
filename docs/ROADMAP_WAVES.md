# Wave roadmap and acceptance state

This file is the machine-readable-ish companion to `docs/AGENT_STATUS.md`. Each wave ends
with a build, a test run, and an updated TEST_MATRIX entry.

| Wave | Gate | State |
|---|---|---|
| 0 | Clean configure/build, project contract | **GREEN** |
| 1 | CPU-only sample passes; ABI and error tests | **GREEN** |
| 2 | Accurate device report on real hardware; CPU fallback | **GREEN** |
| 3 | Vulkan vector-add (and more) passes vs CPU reference | **GREEN** |
| 4 | HCSIR v1 parser/verifier/interpreter + GLSL lowering + GPU execution | **GREEN** — 37/37 tests; the same HCSIR vecadd module passes on the CPU interpreter and on the Vulkan GPU |
| 5 | Async flows/events/graphs; race-free under stress | **GREEN** — 46/46 tests incl. graph replay on both backends; validation profile zero errors |
| 6 | Reduction/scan/matmul suite + benchmark data | **GREEN** for the defined set: reduce, matmul, scan, transpose verified on CPU+Vulkan; benchmark rows recorded. Histogram/image primitives are not in V1 scope. |
| 7 | CUDA source subset migration with report | PARTIAL — scanner tested (PORT-01..03); translator and PTX importer not implemented |
| 8 | GGUF model through verified adapter | PARTIAL — catalog/checksum/path safety tested (MV-01..04); inference adapter not integrated |
| 9 | REST + MCP local-only, authenticated, tested | PARTIAL — MCP (MCP-01..06) and REST (REST-01..05) pass; OpenAPI + remaining endpoints pending |
| 10 | UI pages, settings persistence, no dead controls | RED |
| 11 | Single EXE installer, clean-VM install/uninstall | RED |
| 12 | Stress/soak/fuzz/static analysis; no critical issues | RED |
| 13 | README/matrix match build; release candidate | RED |

## Next concrete task (finish Waves 6–7)

1. Extend `hcsbench` with scan/transpose rows and record raw baseline data.
2. Add a small conformance-tested CUDA source translation subset before any PTX importer.
3. Decide whether histogram/image primitives are in V1 scope; if not, remove them from the
   feature list rather than leaving dead claims.

Deferred from Wave 4: HCSIR serialization/round-trip caching.

## Definition of done for any wave

* `cmake --build --preset windows-tests` succeeds with no new warnings in project code.
* `ctest --preset windows-tests` passes.
* `docs/TEST_MATRIX.md`, `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md` updated.
* No claim in README exceeds what the tests prove.
