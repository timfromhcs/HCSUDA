# Wave roadmap and acceptance state

This file is the machine-readable-ish companion to `docs/AGENT_STATUS.md`. Each wave ends
with a build, a test run, and an updated TEST_MATRIX entry.

| Wave | Gate | State |
|---|---|---|
| 0 | Clean configure/build, project contract | **GREEN** |
| 1 | CPU-only sample passes; ABI and error tests | **GREEN** |
| 2 | Accurate device report on real hardware; CPU fallback | **GREEN** |
| 3 | Vulkan vector-add (and more) passes vs CPU reference | **GREEN** |
| 4 | HCSIR v1 verifier + interpreter; rejection tests | RED |
| 5 | Async flows/events/graphs; race-free under stress | RED |
| 6 | Reduction/scan/matmul suite + benchmark data | PARTIAL (reduce, matmul done) |
| 7 | CUDA source subset migration with report | RED |
| 8 | GGUF model through verified adapter | RED |
| 9 | REST + MCP local-only, authenticated, tested | RED |
| 10 | UI pages, settings persistence, no dead controls | RED |
| 11 | Single EXE installer, clean-VM install/uninstall | RED |
| 12 | Stress/soak/fuzz/static analysis; no critical issues | RED |
| 13 | README/matrix match build; release candidate | RED |

## Next concrete task (Wave 4)

1. Create `src/ir/` with `Module`, `Instruction`, `ValueId` types and a bounded parser for the
   HCSIR v1 text format described in ADR-0004.
2. Implement verification passes listed in ADR-0004; every failure returns a structured
   `HCS_E_*` with line/column context.
3. Implement the CPU interpreter for `HLOAD.F32`, `HSTORE.F32`, `HADD.F32`, `HMUL.F32`,
   `HFMA.F32`, `HSELECT`, `HBRANCH`, `HRETURN`.
4. Add tests: valid module executes and matches the scalar reference; oversized module,
   unknown opcode, type mismatch, unbound value, out-of-range load, unknown version all
   fail with the right code.
5. Keep the existing hand-written kernels untouched as the conformance baseline.

## Definition of done for any wave

* `cmake --build --preset windows-tests` succeeds with no new warnings in project code.
* `ctest --preset windows-tests` passes.
* `docs/TEST_MATRIX.md`, `docs/AGENT_STATUS.md`, `docs/BUILD_LOG.md` updated.
* No claim in README exceeds what the tests prove.
