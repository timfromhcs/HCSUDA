# Wave roadmap and acceptance state

This file is the machine-readable-ish companion to `docs/AGENT_STATUS.md`. Each wave ends
with a build, a test run, and an updated TEST_MATRIX entry.

| Wave | Gate | State |
|---|---|---|
| 0 | Clean configure/build, project contract | **GREEN** |
| 1 | CPU-only sample passes; ABI and error tests | **GREEN** |
| 2 | Accurate device report on real hardware; CPU fallback | **GREEN** |
| 3 | Vulkan vector-add (and more) passes vs CPU reference | **GREEN** |
| 4 | HCSIR v1 parser/verifier/interpreter + GLSL lowering | **PARTIAL** — 34/34 tests green; lowering compiles through `glslc` at build time; HCSIR→GPU execution via an API is still missing |
| 5 | Async flows/events/graphs; race-free under stress | RED |
| 6 | Reduction/scan/matmul suite + benchmark data | PARTIAL (reduce, matmul done) |
| 7 | CUDA source subset migration with report | RED |
| 8 | GGUF model through verified adapter | RED |
| 9 | REST + MCP local-only, authenticated, tested | RED |
| 10 | UI pages, settings persistence, no dead controls | RED |
| 11 | Single EXE installer, clean-VM install/uninstall | RED |
| 12 | Stress/soak/fuzz/static analysis; no critical issues | RED |
| 13 | README/matrix match build; release candidate | RED |

## Next concrete task (remainder of Wave 4)

1. Load the `glslc`-compiled SPIR-V produced by HCSCC into the Vulkan backend and compare GPU
   output against the interpreter for the same module. No public ABI until that passes.
2. Only after that passes: expose HCSIR module compilation through an explicitly experimental
   API entry point (`HCS_E_UNSUPPORTED_FEATURE` for anything outside the verified subset).
3. Add a serialization round-trip (serialize → parse → compare) so modules can be cached.
4. Keep the existing hand-written kernels untouched as the conformance baseline.

## Definition of done for any wave

* `cmake --build --preset windows-tests` succeeds with no new warnings in project code.
* `ctest --preset windows-tests` passes.
* `docs/TEST_MATRIX.md`, `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md` updated.
* No claim in README exceeds what the tests prove.
