# README requirement audit (AGENTS.md 13.2)

| # | Requirement | Status | Evidence |
|---|---|---|---|
| 1 | Original logo + one-sentence value proposition | Done | `ui/assets/brand/` SVG sources; README header and tagline |
| 2 | Accurate badges (CI, release, license) | Done honestly | CI + MIT badges; **no release badge because no GitHub Release exists** (`publication.md`) |
| 3 | Stable / Experimental / Roadmap separation | Done | README "What works today" table + "Roadmap (not in V1)" |
| 4 | Compatibility matrix | Done | README "Compatibility matrix"; `docs/compatibility/cuda-api-matrix.md` |
| 5 | System requirements / Windows versions | Done | README "Requirements" (Windows 11 x64 tested) |
| 6 | Real screenshots with alt text | Done | `docs/screenshots/ui-dashboard.png`, captured from live REST (UI-04/05) |
| 7 | Download/install instructions (single EXE) | Done | `docs/release/installer.md`, README first-run section |
| 8 | First-run guide and "check my GPU" | Done | README "First run — check my GPU" using installed `hcsdoctor` |
| 9 | HDD model storage explanation | Done | README "Memory model (UMA honesty)", KNOWN_LIMITATIONS |
| 10 | Quickstart HCS API + verified kernel | Done | README "Minimal C API example" (vecadd_f32) |
| 11 | CUDA migration example and report | Done | README "CUDA migration status" with real CLI commands/output (PORT-04) |
| 12 | REST + MCP examples, local-only defaults | Done | README "Local REST API and MCP" |
| 13 | Benchmark methodology, hardware, raw JSON | Done | README "Benchmarks", `benchmarks/baselines/`, TEST_MATRIX benchmark section |
| 14 | Build-from-source steps | Done | README "Build from source (verified)" with presets |
| 15 | Troubleshooting/diagnostics | Done | README first-run/troubleshooting, `hcsdoctor`, KNOWN_LIMITATIONS |
| 16 | License, notices, security, contribution links | Done | README "Project documents"/"License", root LICENSE/NOTICE/THIRD_PARTY_NOTICES |
| 17 | NVIDIA non-affiliation statement | Done | README header + UI About section + compatibility notice |
| 18 | Known limitations, not hidden | Done | `docs/KNOWN_LIMITATIONS.md`, `docs/release/v1-scope.md`, RELEASE_CHECKLIST |

No README claim in this repository is backed by simulated data or by a release that was not created.
