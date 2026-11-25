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
| 6 | Reduction/scan/matmul suite + benchmark data | PARTIAL (reduce, matmul done; scan/transpose next) |
| 7 | CUDA source subset migration with report | RED |
| 8 | GGUF model through verified adapter | RED |
| 9 | REST + MCP local-only, authenticated, tested | RED |
| 10 | UI pages, settings persistence, no dead controls | RED |
| 11 | Single EXE installer, clean-VM install/uninstall | RED |
| 12 | Stress/soak/fuzz/static analysis; no critical issues | RED |
| 13 | README/matrix match build; release candidate | RED |

## Next concrete task (Wave 6)

1. Add verified scan/prefix-sum and transpose kernels (CPU reference + Vulkan + odd sizes).
2. Extend `hcsbench` with the new kernels and record raw baseline data.
3. Add repeated randomized schedules to the flow stress tests (Wave 12 stress harness).

Deferred from Wave 4: HCSIR serialization/round-trip caching.

## Definition of done for any wave

* `cmake --build --preset windows-tests` succeeds with no new warnings in project code.
* `ctest --preset windows-tests` passes.
* `docs/TEST_MATRIX.md`, `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md` updated.
* No claim in README exceeds what the tests prove.
