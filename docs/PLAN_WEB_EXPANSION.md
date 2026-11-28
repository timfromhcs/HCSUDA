# Web expansion plan — Vercel demo toward base parity

Created 2026-11-28 from `v0.1.0-rc.2`. This plan governs the next endless Build → Test → Fix →
Deploy → Release loop for `webgpu-demo/` and the corresponding `main` merge/release.

## Truthful scope (non-negotiable)

* The browser app is a **standalone demo**, not the native HCSUDA runtime. It uses no Vulkan
  backend, C ABI, model vault or local service.
* Every kernel result is checked against a real CPU/WASM reference. No fabricated correctness.
* Timings are wall-clock around submit + readback including transfer overhead; label them as such.
* Visuals/games are explicitly labelled "demo/synthetic" and never presented as GPU telemetry.
* Inputs are bounded (element counts, matrix dimensions, repetitions) to avoid browser hangs.

## Feature phases

### Phase 1 — kernel parity core
- [x] WASM reference module extended from vecadd to: scale, reduce, scan, matmul, transpose,
      histogram (same shapes as native built-ins).
- [x] WGSL shaders for each supported kernel on WebGPU.
- [x] Node gate `test-wasm.mjs` covers every WASM reference with boundary sizes.
- [x] UI kernel selector dispatches to the chosen WebGPU kernel and validates against WASM.

### Phase 2 — customizable runs and downloadable results
- [x] Controls: kernel, element/matrix size, alpha, groups/workgroup size, backend choice
      (WASM / WebGPU / both), warmup and repetitions.
- [x] Result table with actual values or bounded preview, worst error and pass/fail.
- [x] Download results as JSON and CSV via Blob (no server, no telemetry).

### Phase 3 — visuals, benchmarks, interactive race
- [x] Canvas visualizer: buffer heatmap/bar view and per-run timing bars from measured values.
- [x] Benchmark mode: median/p10/p90 over configured reps, CPU vs GPU comparison, downloadable CSV.
- [x] "Kernel race" mini-game: user picks parameters, races WASM vs WebGPU, scoreboard kept in
      localStorage; clearly labelled demo/synthetic with no performance claim.

### Phase 4 — main merge, gates, release
- [x] CI job verifies every WASM reference (extend `web-demo` job).
- [x] Deploy to Vercel production, verify public alias returns 200 for all assets.
- [ ] Rebuild local single-file EXE, verify checksum/import scan.
- [ ] Update README, release notes, TEST_MATRIX and release checklist.
- [ ] Publish annotated tag `v0.1.0-rc.3` and GitHub Release with EXE, checksum, SBOM.
- [ ] Hosted CI 8/8 green on the merged `main` commit.

## Acceptance criteria

1. Every advertised web kernel has a passing WASM reference test and a WebGPU result check.
2. All controls change real computation; no dead buttons.
3. Exports contain measured values, parameters and timestamps; no invented data.
4. Public Vercel deployment is READY and serves index/app.js/kernel.wasm with 200.
5. Local release gates remain green: 96/96 tests, 8/8 CI jobs, secret/OpenAPI/artifact gates.
6. No claim that the browser demo replaces the native runtime or is faster than it.

## Loop protocol

Each pulse: re-read this plan, pick the smallest unchecked item, implement, build/test/fix, deploy
when the web artifact changes, commit/push to `main`, record evidence, then repeat. Never mark an
item green on inspection alone.
