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
| Project phase | 0.1.0 release candidate; Waves 0–13 implemented, 84/84 tests green |
| Test evidence | `docs/TEST_MATRIX.md` (API/CPU/VK/IR/FL/GR/KC/PORT/MV/MCP/REST/TR/UI/INS/SEC/SOAK/CI/SA/SET/STR) |
| Hardware | Ryzen 7 7735HS + Radeon 680M (Vulkan 1.4.315), Windows 11 Pro 26200 |

**Conclusion:** there is no outstanding branch work. The remaining work is *gap closure*, not merging.

## 2. Classification of "open points"

### 2.1 Internally closable (this loop works on these)
- **DOC-STALE:** `docs/KNOWN_LIMITATIONS.md` contradicts `AGENT_STATUS.md` / `TEST_MATRIX.md`:
  claims cross-version upgrade untested (INS-04 passed), no OpenAPI (file exists + SET-05),
  no translation (TR-01..03 passed), no settings UI (UI-03 passed), installer upgrade untested,
  "0.1.0 development build" header vs RC status. This is a *truthful-copy release blocker*.
- **DOC-STALE:** README line 85 says "no translation in V1" while TR-01..03 prove translation exists.
- **CACHE:** persistent shader/module cache deferred (HCSIR binary serialization exists, IR-21..23).
- **KERNEL:** global reduce (currently two-stage) and global scan (currently chunk-local) — optional
  correctness surface, only add if backed by tests.
- **IR:** elementwise subset only; integer/bitwise opcodes can be added with verifier + tests.
- **UI:** no automated screenshot evidence; no native shell (explicit V1 scope decision ADR-0003 —
  do **not** reopen without user authorization).
- **BENCH:** no timestamp queries; wall-clock numbers include dispatch overhead (documented).

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

Made with love by HCSMedia & timfromhcs.
