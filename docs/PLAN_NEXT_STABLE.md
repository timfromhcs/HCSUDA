# Plan to the next stable version (v1.0.0)

Created 2026-11-28 from the verified `v0.1.0-rc.2` state. This plan is a working contract for the
endless Build → Test → Fix → Record → Push loop. It does **not** declare v1.0.0 stable early.

## 0. Starting state (measured)

| Item | Current evidence |
|---|---|
| Release | `v0.1.0-rc.2` on GitHub: local EXE + SHA-256 + SBOM |
| Cloud | `https://hcsuda-webgpu-demo.vercel.app` (standalone WebGPU/WASM demo, READY, public) |
| Tests | 96/96 HCS tests; 93/93 CPU-only; 96/96 validation layers |
| CI | 8/8 hosted jobs green (run IDs in `docs/TEST_MATRIX.md`) |
| Soak | 60 min, 1418 iterations, 0 failures (SOAK-02) |
| Gates | secret scan (140 files), OpenAPI, artifact checksum/import scan, WASM reference |
| Repo | `main` == `origin/main`, no open branch diff |

## 1. Stable-version definition

v1.0.0 may be claimed only when **all** of the following are true and evidenced:

1. Every V1 release criterion in `AGENTS.md` §18 is either green or explicitly scoped out with a
   written reason.
2. The three open external actions in `docs/release/EXTERNAL_BLOCKERS.md` are completed with real
   evidence (signing, clean VM, cross-vendor hardware).
3. Signed installer exists, checksum and SBOM are regenerated for the exact signed binary.
4. A final GitHub Release `v1.0.0` is published with matching README, notes and compatibility copy.
5. No P0/P1 bug and no known critical/high security issue remains.

No feature expansion is a precondition for stability. Optional features in §4 stay out of v1.0.0
unless their own gates pass first.

## 2. Phase A — external gates (owner actions, blocking)

| # | Gate | Owner action | Evidence required |
|---|---|---|---|
| A1 | Code signing | Obtain OV/EV certificate; sign EXE with `signtool sign /sha1 <thumb> /fd SHA256 /tr <RFC3161>`; verify with `signtool verify /pa` | Command output + thumbprint (no private key in repo) |
| A2 | Clean VM | Disposable Windows 11 x64 VM without dev tools; verify checksum, run EXE, `hcsdoctor --json`, `hcsbench --device 0`, uninstall | VM build, outputs, INS-05 row in TEST_MATRIX |
| A3 | Cross-vendor | Intel Arc/Iris and NVIDIA devices with current drivers; `hcsdoctor --json`, ctest, hcsbench | Vendor/device IDs, driver versions, VK-07/VK-08 rows |

Exact commands and refusal rules are in `docs/release/EXTERNAL_BLOCKERS.md`. While any of A1–A3
is open, the product remains a release candidate — no stable claim in README, UI or release notes.

## 3. Phase B — internal hardening (agent loop, non-blocking but required before tag)

1. **Re-run every gate after each change**: `scripts/build-test-once.ps1`, `ctest --preset windows-tests`,
   `ctest --preset windows-cpu-only`, `ctest --preset windows-validation`, `scripts/check-secrets.ps1`,
   `scripts/check-openapi.ps1`, `scripts/check-artifact.ps1`, `node webgpu-demo/test-wasm.mjs`.
2. **Fuzz/stress**: repeat bounded fuzz and soak with the final binary; record raw outputs.
3. **Static analysis**: `cmake --build --preset windows-analyze --parallel` → zero warnings.
4. **Dependency/SBOM**: no new dependencies; regenerate SBOM after the signed EXE is final.
5. **Artifact integrity**: signed EXE checksum published; no secrets/forbidden imports; no driver or
   loader modification (INS-01..04).
6. **Documentation consistency**: README, RELEASE_NOTES, TEST_MATRIX, KNOWN_LIMITATIONS,
   RELEASE_CHECKLIST, v1-scope and compatibility matrix must all agree with the tagged build.
7. **Cloud parity**: redeploy `webgpu-demo` after any demo change; confirm the stable alias returns
   200 for index/app.js/kernel.wasm and that the scope disclaimer remains visible.

## 4. Optional roadmap (NOT v1.0.0 preconditions)

These remain documented as deferred/out of scope; each needs its own tests before any claim:

* GGUF inference adapter with a tested llama.cpp Vulkan backend and published quantisation support.
* Textual PTX importer for a verified opcode subset; CUBIN/SASS stays unsupported.
* Native Qt/WebView2 shell (ADR-0003 currently selects the local browser console).
* Streamable HTTP MCP with localhost-only authentication and Origin validation.
* Additional UI sections: Translate, Benchmarks, Visualizer, API/MCP, Diagnostics.
* GPU timestamp queries, allocator pooling, IR integer/bitwise opcodes, multi-GPU.

## 5. Loop protocol until stable

Every 2-minute pulse: do not wait. Each cycle:

1. Read this plan, `docs/AGENT_STATUS.md`, `docs/TEST_MATRIX.md`, `logs/limitless-loop.log`.
2. Pick the smallest open item from §2/§3 (or a real defect found by a gate).
3. Implement → build → test → root-cause/fix → update docs → commit → push to `main`.
4. Re-run hosted CI and record the run ID.
5. If an item is blocked by owner/hardware, record the exact blocker and move to the next item.
6. Never mark an item green on inspection alone; evidence or it did not happen.

## 6. Exit

When §1 is fully satisfied, create annotated tag `v1.0.0`, publish the GitHub Release with the
signed EXE, checksum, SBOM and notes, update README to stable, and record the final evidence in
`docs/TEST_MATRIX.md` and `docs/release/RELEASE_CHECKLIST.md`. Until then: `v0.1.0-rc.2` is the
honest current release.

Made with love by HCSMedia & timfromhcs.
