# Limitless Work Plan — HCSUDA V1 "no open points" drive

Created: 2026-11-28. Owner: autonomous agent loop. Language: German request / English repo copy.

## 1. Analysis result (re-read before every loop)

| Item | Finding |
|---|---|
| Working tree | **Clean** (`git status --porcelain` empty, no staged diff) |
| Branch | Only `main` at `fde331d`, identical to `origin/main` |
| Other branches | **None** (`git branch -a` shows only `main` + `origin/main`) |
| Diff to main / branches | **Empty** — nothing to port or merge |
| Remote | `https://github.com/timfromhcs/HCSUDA.git` (fetch+push) |
| Project phase | 0.1.0 release candidate; Waves 0–13 implemented, 90/90 tests green |
| Test evidence | `docs/TEST_MATRIX.md` (API/CPU/VK/IR/FL/GR/KC/PORT/MV/MCP/REST/TR/UI/INS/SEC/SOAK/CI/SA/SET/STR) |
| Hardware | Ryzen 7 7735HS + Radeon 680M (Vulkan 1.4.315), Windows 11 Pro 26200 |

**Conclusion:** there is no outstanding branch work. The remaining work is *gap closure*, not merging.

## 2. Classification of "open points"

### 2.1 Internally closable (this loop works on these)
- **[CLOSED] DOC-STALE:** `docs/KNOWN_LIMITATIONS.md` contradicted `AGENT_STATUS.md` /
  `TEST_MATRIX.md` (cross-version upgrade, OpenAPI, translation, settings UI, installer upgrade,
  version header). Fixed together with README, release notes, publication copy, v1-scope and the
  `hcsport` tool header. Release identity aligned to the actual `0.1.0` binary version.
- **[CLOSED] PRESETS:** no `ctest` preset existed for cpu-only/release/relwithdebinfo/vulkan;
  added and verified (CPU-only 87/87, validation 90/90).
- **[CLOSED] EVIDENCE:** `hcsport --convert` CLI exercised end-to-end (PORT-04); a real UI
  screenshot captured from the live REST service (UI-04) and embedded in README.
- **[CLOSED] CACHE:** persistent shader cache. Implemented as a bounded, device-fingerprinted
  on-disk `VkPipelineCache` (VK-06), configured through the existing `shader_cache_dir` setting
  via an internal setter — no stable C ABI change. Corrupt/stale/oversized blobs are ignored.
  HCSIR binary serialization remains separate and is not used as a runtime module cache.
- **[PARTIALLY CLOSED] KERNEL:** `reduce_final_f32` completes the two-stage reduction (KC-07/08)
  and `scan_add_offsets_f32` completes a host-free global scan pipeline (KC-09/10), both on CPU
  and Vulkan. Histogram/image kernels, IR integer/bitwise opcodes and timestamp queries remain
  documented limitations; they expand the declared V1 scope and are deferred rather than claimed.
- **[DEFERRED — explicit ADR-0003] UI:** no native Qt/WebView2 shell. A real screenshot now exists;
  automated screenshot regression remains out of scope.

### 2.2 External blockers (cannot be closed by code; report, never fake)
- Code signing: no publisher certificate configured → installer stays unsigned + SHA-256.
- GitHub Release publication: requires manual approval by repo owner.
- Clean-VM install test: needs a disposable Windows 11 VM not available in this environment.
- Cross-vendor hardware: only Radeon 680M + CPU available; Intel/NVIDIA verification impossible here.

**Rule:** "no open points" is interpreted as *no internally closable point open*. External items are
tracked as "Not verified / owner action", never marked green.

## 3. Loop protocol (Build → Test → Fix → Record → Repeat)

Every iteration, in order:

1. Re-read `docs/AGENT_STATUS.md`, `docs/TEST_MATRIX.md`, `docs/KNOWN_LIMITATIONS.md`, this file.
2. Pick **one** smallest closable point from §2.1; write acceptance test first.
3. Implement minimal surgical diff; no scope expansion into §2.2 or ADR-0003 decisions.
4. Build:
   ```powershell
   cmake --preset windows-tests
   cmake --build --preset windows-tests --parallel
   ```
5. Test:
   ```powershell
   ctest --preset windows-tests --output-on-failure
   ```
6. If failure: reproduce → root cause → regression test → fix → rerun full suite. Never mark green
   on "looks right".
7. Update `docs/BUILD_LOG.md`, `docs/TEST_MATRIX.md`, `docs/AGENT_STATUS.md` and any limitation
   row changed; commit coherent tested changes with Conventional Commits.
8. Review diff for secrets, false claims, generated files, license issues.
9. Repeat from step 1 until §2.1 is empty.

A background PowerShell loop (`scripts/loop-build-test.ps1`) re-runs build+test continuously and
writes `logs/limitless-loop.log`; a 2-minute automation sends a reminder prompt into this chat so
no iteration is skipped. Reminders are a pulse, **not** a wait state — work continues between them.

## 4. Exit criteria (honest)

- [ ] All §2.1 items closed or explicitly reclassified with a written reason.
- [ ] Full suite green in `windows-tests`, `windows-cpu-only`, `windows-validation`.
- [ ] Docs agree with code and evidence; zero contradictory claims.
- [ ] No P0/P1 bug, no known critical/high security issue.
- [ ] §2.2 items listed verbatim as owner/external actions with exact commands/links.
- [ ] Reminder automation deleted only after the loop is declared stable.

## 5. Final state (2026-11-28)

**Internally stable: yes.** All §2.1 points are closed or explicitly reclassified as intentional
V1 scope decisions with a written reason. No branch diff existed to merge. Verified green:

* `windows-tests`: configure/build/test exit 0; 85/85 HCS tests; repeated background cycles all
  green (`logs/limitless-loop.log`).
* `windows-cpu-only`: build exit 0; 81/81 tests via `ctest --preset windows-cpu-only`.
* `windows-validation`: build exit 0; 85/85 tests, zero validation errors.
* `windows-analyze`: zero MSVC `/analyze` warnings in project-owned code.
* `scripts/check-secrets.ps1`: PASS, 118 tracked files.
* Real CLI evidence PORT-04; real UI screenshot UI-04.
* Two coherent commits: `1228dcf`, `6166227`.

**Not closable by this agent (owner/external actions, kept honest, never marked green):**
clean-VM install test, code signing (no certificate), manually approved GitHub Release/tag,
Intel/NVIDIA cross-vendor hardware verification. These are listed in `docs/AGENT_STATUS.md` and
`docs/release/v1-scope.md`; no release copy claims them as done.

The background loop was stopped once, then **restarted on explicit user request** together with
a new 2-minute pulse; work continues with periodic commits/pushes to `main`. Newly closed in the
endless phase: automated UI capture (`hcs_ui_capture`, UI-05). External blockers were re-checked:
no signing certificate, no second GPU, no Windows VM image — they remain owner/external actions.

Made with love by HCSMedia & timfromhcs.
