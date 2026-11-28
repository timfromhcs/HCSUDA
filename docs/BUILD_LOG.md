# Build log

Chronological record of real build/test commands and outcomes. No simulated results.

## 2026-11-25 — Wave 0 reconnaissance

```
git --version            -> 2.54.0.windows.1
cmake --version          -> 4.4.0
ninja --version          -> 1.13.2
cl                       -> absent from PATH; MSVC 14.44.35207 found under VS 2022 Community
vulkaninfo --summary     -> Vulkan 1.4.357 instance, AMD Radeon(TM) Graphics, 1.4.315
$env:VULKAN_SDK          -> C:\VulkanSDK\1.4.357.0
glslc                    -> C:\VulkanSDK\1.4.357.0\Bin\glslc.exe
```

Not installed: Qt, Inno Setup. WebView2 runtime present. GitHub reachable.

## 2026-11-25 — Configure (RelWithDebInfo, Vulkan + tests)

```
cmake --preset windows-tests
-- The CXX compiler identification is MSVC 19.44.35228.0
-- HCSUDA 0.1.0: Vulkan backend ON, tests ON
-- Build files have been written to: D:/HCSUDA/build/windows-tests
```

## 2026-11-25 — First build attempts

1. Failure: `min`/`max` macros from `windows.h` collided with `std::min` in
   `cpu_backend.cpp` (C2589/C2059). Fix: global `NOMINMAX` + `WIN32_LEAN_AND_MEAN`.
2. Failure: `VkPhysicalDeviceFeatures` has no `shaderFloat16`/`shaderInt8`
   (they live in `VkPhysicalDeviceVulkan12Features`). Fix: query via
   `vkGetPhysicalDeviceFeatures2` when instance API >= 1.2, otherwise do not claim them.
3. Failure: member name `device_` collided between the HCS device record and `VkDevice`.
   Fix: renamed the record pointer to `deviceRecord_`.
4. Failure: generated SPIR-V arrays had internal linkage (file-scope `const` in C++).
   Fix: emit `extern const ...` in `cmake/embed_spirv.cmake`.

## 2026-11-25 — Build success

```
cmake --build --preset windows-tests --parallel
hcsuda_core.lib
hcsdoctor.exe
hcs_tests.exe
hcsbench.exe
```

## 2026-11-25 — Test iterations

```
ctest --preset windows-tests
18 tests, 2 failures (cpu_reduce, vulkan_reduce)
```

Root cause: the reduce tests wrote the group count to push-constant offset 8 while the
kernels read offset 4. Fixed by introducing `ReducePush { n, groups, ... }` in the tests.
Not a kernel bug; the kernels and CPU backend agreed once the layout matched.

## 2026-11-25 — All tests pass

```
ctest --preset windows-tests
1/1 Test #1: hcs_unit ... Passed  0.99 sec
100% tests passed
```

## 2026-11-25 — Benchmarks

Initial run reported `verified:false` for vecadd. Diagnosis: `hcsbench` zero-initialised
`hcsDispatchDesc` and set only `group_count_x`, leaving y/z at 0, which Vulkan correctly
treats as an empty dispatch. Fixes:

* `hcsDispatch` now rejects any zero group count (fail-fast instead of silent no-op).
* `hcsbench` sets y/z explicitly for every dispatch.

After the fix, all vecadd samples verify against a double-precision host reference. The
pre-fix "fast" numbers were the no-op dispatch and are discarded, not reported.

Final reference numbers are recorded in `README.md` and
`benchmarks/baselines/radeon680m-2026-11-25.json`.

## 2026-11-25 — Wave 4: HCSIR v1 parser, verifier, interpreter

New code: `src/ir/hcsir.h`, `src/ir/hcsir.cpp`, `tests/unit/test_ir.cpp`, wired into
`hcsuda_core` and `hcs_tests`.

Iterations and real failures fixed:

1. `tests/unit/test_ir.cpp` could not include `ir/hcsir.h` (C1083). The library's include
   directory is `PRIVATE`; added `${CMAKE_CURRENT_SOURCE_DIR}/src` to the test target.
2. A test module failed with `unknown opcode '0.0'`. Root cause: the grammar had no way to
   name a constant, so a bare literal assignment had no defined meaning. Fixed by adding a
   real declaration form — `const <name> : <type> = <literal>` — plus `ConstantDecl` in the
   module and constant initialisation in the interpreter. A test-only workaround was rejected
   because the gap was in the language, not the test.
3. `ir_branch_and_select_execute` produced `y[0] == 0`. Root cause: the interpreter used a
   single `done` flag both to terminate the current block and to finish the invocation, so the
   first `branch` ended the invocation before the target block ran. Fixed by separating
   `terminated` (block reached its terminator) from `finished` (invocation returned).
4. Diagnostics reported the filtered line index instead of the source line. Fixed by tracking
   line numbers alongside the trimmed lines.

Results:

```
cmake --build --preset windows-tests --parallel   -> hcsuda_core.lib, hcs_tests.exe
ctest --preset windows-tests --output-on-failure  -> 100% tests passed, 30/30 HCS tests
```

Not done in this wave (and not claimed): HCSIR → GLSL/SPIR-V lowering, serialization,
integer/bitwise opcodes, atomics, subgroup or matrix operations.

## 2026-11-25 — Wave 4 (continued): HCSIR → GLSL lowering

New code: `src/ir/lower_glsl.cpp`, `tools/hcscc/main.cpp`, `tests/unit/test_ir_glsl.cpp`,
`tests/fixtures/ir/vecadd.hcsir`. The lowering emits a GLSL 450 compute shader with a bounded
state machine over HCSIR blocks, storage buffers, push constants for parameters, and bounds
checks on every load/store against the runtime array length. Barriers are rejected with
`HCS_E_UNSUPPORTED_FEATURE` until uniform-control-flow analysis exists.

```
cmake --build --preset windows-tests --parallel
  hcscc: lower HCSIR fixture to GLSL
  kernel=ir_vecadd local_size=64 blocks=1 instructions=8 buffers=3 params=1 constants=1 glsl_bytes=1180
  glslc: validate generated GLSL from hcscc

ctest --preset windows-tests --output-on-failure
  100% tests passed; 34/34 HCS tests (4 new lowering tests)
```

The build fails if either `hcscc` or `glslc` rejects the generated shader. This proves the
emitted GLSL is syntactically valid for Vulkan 1.1; it does **not** prove GPU execution of
HCSIR modules, because no runtime SPIR-V program-loading API exists yet.

## 2026-11-25 — Wave 4 complete: generated HCSIR executes on Vulkan

Added the experimental program API (`hcsProgramCreateSpirv`, `hcsProgramDestroy`,
`hcsKernelFromProgram`) to the stable header, with a Vulkan pipeline keyed by a unique kernel
uid and an explicit `HCS_E_UNSUPPORTED_FEATURE` on the CPU backend. Generated SPIR-V from the
fixture is embedded into a separate `hcsuda_ir_spv` static library so the build graph stays
acyclic (`hcscc` links `hcsuda_core`; `hcsuda_core` must not depend on `hcscc`).

One warning was fixed: `fopen` deprecation in `hcscc` (`_CRT_SECURE_NO_WARNINGS` on MSVC).

```
cmake --build --preset windows-tests --parallel   -> all targets, no project warnings
ctest --preset windows-tests --output-on-failure  -> 37/37 HCS tests
.\hcs_tests.exe --filter hcsir_generated          -> PASS (real Vulkan dispatch)
```

HCSIR vecadd now has two independent execution paths that agree: the CPU interpreter and a
GPU pipeline compiled from HCSCC-generated GLSL.

## 2026-11-25 — Wave 5 (first slice): async HCSFlow and HEvent

The Vulkan dispatch now allocates a command buffer, descriptor set and fence per submission.
With a flow, `hcsDispatch` submits and returns without waiting; the flow owns the fences and
`hcsFlowSynchronize` waits with the caller's timeout. Without a flow, the original synchronous
behaviour is preserved. Descriptor sets are never reset while in flight; on pool exhaustion
the runtime waits for device idle, resets the pool once and clears the flows' pending bookkeeping.

HEvent is a flow-order marker: recording captures the flow's submission counter, and waiting
polls a monotone completion counter derived from `vkGetFenceStatus` in submission order. The CPU
backend supplies the same semantics through its synchronous flow.

Compile errors fixed during this slice:
* newly used `vkFreeDescriptorSets`, `vkFreeCommandBuffers` and `vkGetFenceStatus` were absent
  from the dynamic-loader function table;
* `vkAllocateCommandBuffers` was declared twice in that table;
* a loop variable named `flow` shadowed the dispatch parameter;
* tests took the address of a temporary dispatch descriptor.

One test expectation was corrected after observing real behaviour: an event recorded on an idle
flow is already satisfied (target 0), so the timeout path is now tested with a mock backend flow
instead of relying on GPU scheduling.

```
cmake --build --preset windows-tests --parallel   -> all targets built
ctest --preset windows-tests --output-on-failure  -> 42/42 HCS tests
```

## 2026-11-25 — Validation-layer profile exposed two real bugs

A dedicated `windows-validation` configure/build/test preset now explicitly enables
`VK_LAYER_KHRONOS_validation` (the build fails if the layer is not installed; it is never a
runtime dependency). Running the suite under it found:

1. `vkFreeDescriptorSets` requires `VK_DESCRIPTOR_POOL_CREATE_FREE_DESCRIPTOR_SET_BIT`; the
   pool was created with flags 0 (VUID 00312). Fixed by setting the flag.
2. `VulkanContext` kept raw pointers to flows after `hcsFlowDestroy`, then called `detach()` on
   freed objects at shutdown. This hung the flow tests under validation. Fixed by registering
   `(hcs_flow_t*, VulkanFlow*)` pairs and removing the pair in a new `destroyFlow` backend hook.

Removed a temporary unused `skipped` variable and added `fflush(stdout)` in the test harness so
a hung CTest can be localized from redirected output.

```
cmake --preset windows-validation
cmake --build --preset windows-validation --parallel
ctest --preset windows-validation --timeout 180 --output-on-failure
  -> 100% tests passed, 42/42 HCS tests, no validation errors
```

## 2026-11-25 — Wave 5 complete: bounded HCSGraph subset

Added experimental graph API (`hcsGraphCreate`, `hcsGraphAddNode`, `hcsGraphCompile`,
`hcsGraphRun`, `hcsGraphDestroy`). Nodes are added in topological order and dependencies must
reference strictly earlier nodes; that makes cycles impossible by construction. Compile
validates the whole graph, run replays nodes sequentially on one flow and reuses the normal
dispatch path (including flow submission counting). No parallel DAG scheduling is claimed.

```
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure -> 46/46 tests
cmake --build --preset windows-validation --parallel
ctest --preset windows-validation --timeout 180 --output-on-failure -> 46/46, zero validation errors
```

The first build failed because the graph tests used bare `c`/`a`/`d` instead of `data.c` etc.;
that was a test typo, not a runtime fault.

## 2026-11-25 — Wave 6 (first slice): scan and transpose

Added `shaders/hksl/scan.comp` and `transpose.comp`, CPU reference implementations, kernel
registry entries and six tests (KC-01..KC-06).

Three real build/test failures were fixed:

1. GLSL reserves `input` and `active`; the buffer was renamed `src_data`/`dst_data` and the
   lane predicate `inChunk`.
2. The new kernel names were added to the registry with an edit whose whitespace did not match
   the source, so `hcsKernelCreate("scan_f32")` returned `HCS_E_KERNEL_NOT_FOUND`. Fixed with an
   exact edit and verified by the tests.
3. The Vulkan scan read all 256 lanes even when the chunk was only 157 elements, so later
   workgroups scanned across chunk boundaries. Fixed by masking lanes with `lid < chunk`. The
   pre-fix numbers were discarded, not reported.

```
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure -> 52/52
ctest --preset windows-validation --timeout 180 --output-on-failure -> 52/52, zero validation errors
```

## 2026-11-25 — Wave 7 (first slice): hcsport compatibility scanner

Added `src/translation/cuda_source/scanner.{h,cpp}`, `tools/hcsport/main.cpp`, a MIT-licensed
CUDA fixture (`tests/fixtures/cuda/vecadd.cu`) and three tests (PORT-01..03). The scanner is
bounded (4 MiB source, 16 KiB lines, 4096 findings), rejects binary input, and classifies each
detected CUDA construct as `manual_port_required` or `unsupported`. It deliberately has no
`Translated` category yet, reports `translated: 0`, and `--convert` exits with an explicit
"not implemented in V1" message.

```
.\hcsport.exe --scan tests\fixtures\cuda\vecadd.cu
  bytes 857, lines 29, manual_port_required 14, unsupported 1, translated 0
ctest --preset windows-tests --output-on-failure -> 55/55
```

Build fix: `hcsport` needed the private `src` include directory, like `hcscc`.

## 2026-11-25 — Benchmarks extended

`hcsbench` now measures scan and transpose as well. A local variable named `count` shadowed an
outer one; renamed to `elements`. Real medians (warm-up 5, 25 repetitions, Radeon 680M):
scan 0.088 ms, transpose 0.134 ms; CPU 1.133 ms and 1.061 ms respectively. Raw results were
appended to `benchmarks/baselines/radeon680m-2026-11-25.json`. These numbers include submit +
fence wait and are not pure kernel timings.

## 2026-11-25 — Wave 8 (first slice): ModelVault catalog

Added `src/model_store/sha256.{h,cpp}` (FIPS 180-4, no dependency),
`src/model_store/model_vault.{h,cpp}` (bounded JSON-lines catalog, canonical-path allowlist,
GGUF/format inspection, atomic writes), `docs/api/model-vault.md` and four tests (MV-01..04).

The generated catalog stores `size_bytes` as a quoted string so the in-tree line parser can use
one quoted-value routine for every field; it stays valid JSON.

```
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure -> 59/59
ctest --preset windows-validation --timeout 180 -> 59/59, zero validation errors
```

No model file is bundled in the repository or build tree; tests create temporary fixtures.

## 2026-11-25 — Wave 9 (first slice): MCP stdio server

Added `src/api/mcp/mcp_server.{h,cpp}`, `tools/hcsbridge-mcp/main.cpp`, `docs/api/mcp.md` and
six protocol tests (MCP-01..06). The server negotiates MCP protocol 2025-11-25, exposes only
four read-only tools, and rejects malformed/oversized/deep messages with JSON-RPC -32600.

Two build/test corrections were needed:
* the executable initially lacked the private `src` include path (C1083 on `api/mcp/mcp_server.h`);
* two C++ string-literal concatenations were invalid (`const char* + const char*`);
* tests must search for escaped substrings because the JSON result is itself embedded as a
  text field (the wire output is correct).

```
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure -> 65/65
ctest --preset windows-validation --timeout 180 -> 65/65, zero validation errors
echo initialize | hcsbridge-mcp.exe -> valid initialize response
```

## 2026-11-25 — Wave 9 (continued): local REST API

Added `src/api/rest/rest_server.{h,cpp}`, `tools/hcsbridge-rest/main.cpp`, `docs/api/rest.md`
and four unit tests (REST-01..04), plus a live loopback check (REST-05).

Build fixes followed the same pattern as earlier tools: private `src` include path for the
executable and a missing `hcsuda.h` include (the tool calls `hcsInit`/`hcsShutdown`).

Live check with a fixed test token on port 18710:

```
health=200 {"status":"ok","local_only":true}
unauthorized=401
devices=200 {"devices":[{"index":0,"name":"AMD Radeon(TM) Graphics",...}]}
```

The server binds 127.0.0.1 only; no remote binding option exists.

## 2026-11-25 — Wave 7 (continued): narrow CUDA source translation

Added `src/translation/cuda_source/translator.{h,cpp}`, `docs/api/hcsport.md` and three tests
(TR-01..03). `hcsport --convert` now emits HCSIR v1 for exactly one documented vecadd kernel
shape and fails closed otherwise.

Four parsing defects were found by the tests and fixed:
1. the signature slice stopped at `(` so no parameters were seen;
2. the trailing `)` was part of the last parameter name;
3. scalar identifiers retained the type prefix (`floatalpha` instead of `alpha`);
4. the checked-in fixture has no scalar factor, so the translator now synthesises a constant
   `1.0` and emits `add` instead of requiring `alpha`.

CLI check produced a valid module and a report with `translated:1`, `host_manual:13`,
`host_unsupported:1`.

```
ctest --preset windows-tests --output-on-failure -> 72/72
ctest --preset windows-validation --timeout 180 -> 72/72, zero validation errors
```

## 2026-11-25 — Wave 10 (first slice): local browser UI

Added `src/api/rest/ui_page.{h,cpp}` and a public `GET /ui/` route on the local REST service.
The page is self-contained HTML/CSS/JS with no remote resources. It injects the process bearer
token for same-origin requests and renders only data returned by `/api/v1/health`, `/version`,
`/runtime/status` and `/memory/budget`. Missing metrics render as "Not exposed by this
driver/API"; there are no fabricated utilization or temperature values.

```
ctest --preset windows-tests --output-on-failure -> 73/73
live: GET http://127.0.0.1:18711/ui/ -> 200 text/html, 4381 bytes
```

No screenshot was produced by an automated browser, so none is claimed as product evidence.

## 2026-11-25 — Wave 11: single-file installer

IExpress and 7-Zip SFX were tried first: IExpress left a modal dialog and 7z.sfx required
elevation; 7zCon.sfx accepted the archive but never executed the install script in this
environment. Replaced both with a native C++ setup executable (`tools/hcsuda_setup/main.cpp`)
that embeds a bounded generated payload and launches the install script directly.

```
cmake --build --preset windows-tests --parallel
  -> artifacts payload embedded, HCSUDA-Setup-x64.exe built
powershell -File installer\make_installer.ps1
  -> artifacts/HCSUDA-Setup-x64.exe + SHA-256
powershell -File tests\installer\test_installer.ps1 -> INSTALLER_TEST_PASS
full EXE run -> SETUP_EXIT=0, installed binaries report real devices, uninstall OK
```

Final installer hash: F418108B57CEDD61AE60EAB44CAF3EEC342B3173FB2A73F0BFFB62AFF7985C05
(unsigned; checksum is the verification mechanism).

## 2026-11-25 — Wave 12/13: stress, fuzz, OpenAPI, SBOM, scope

Added `tests/unit/test_stress.cpp` (STR-01..03). The 500-submission GPU stress test exposed a
real driver quirk: `vkWaitForFences` with hundreds of fences returned `VK_ERROR_UNKNOWN` on the
AMD driver. Fixed by waiting in batches of 64 in `VulkanFlow::synchronize`. Validation layers then
also confirmed no leaked Vulkan objects in the happy path.

```
ctest --preset windows-tests --output-on-failure -> 76/76
ctest --preset windows-validation --timeout 180 --output-on-failure -> 76/76, zero validation errors
```

Added `docs/api/openapi.json`, `docs/release/sbom.json` and `docs/release/v1-scope.md`. The scope
document explicitly omits GGUF inference, PTX import, Qt/WebView2 shell, Streamable HTTP and
several advanced compute features from V1 claims rather than leaving them vague.

## 2026-11-25 — remaining gate work

* `tests/installer/test_upgrade.ps1`: in-place reinstall keeps user files, the reinstalled
  binary reports real devices, and uninstall leaves the Vulkan loader intact — `UPGRADE_TEST_PASS`.
  This is a same-version reinstall test, not a cross-version upgrade test.
* `scripts/check-secrets.ps1`: scanned 104 tracked files — `SECRET_SCAN_PASS`. Dummy tokens in
  docs/tests are excluded by path; no credential patterns remain in shipped code.
* CI expanded with installer/upgrade/secret-scan jobs. The validation job is
  `continue-on-error` because hosted runners may lack the Vulkan SDK; real hardware QA is a
  separate self-hosted concern and is never silently marked passed.
* A bounded 60-minute soak (`tests/stress/soak.ps1`) was started in the background and writes
  real medians to `artifacts/soak.log`. No result is claimed until it finishes.

## 2026-11-25 — Settings persistence and soak restart

Added `src/service/settings.{h,cpp}` with bounded JSON persistence, validation, defaults and
reset, plus REST GET/POST/reset endpoints and working Save/Reset controls in the local UI.
Five settings tests and one REST round-trip test were added.

A first 60-minute soak attempt failed because a concurrent rebuild replaced `hcsbench.exe`
while it was running; no soak result was claimed. The script now copies its binary to a private
temp path before starting, and the soak was restarted after all builds finished.

```
ctest --preset windows-tests --output-on-failure -> 81/81
ctest --preset windows-validation --timeout 180 -> 81/81, zero validation errors
installer SHA-256: 29831820EAD55E5FD1C6C26D06AC04ADB3713723EC4C338693720EC74A4F3395
```

## 2026-11-26 — 60-minute soak result

`tests/stress/soak.ps1` completed 1610 iterations against a private copy of `hcsbench.exe`
(no rebuild interference). All vecadd samples verified; zero errors or failed dispatches.

Real observation: vecadd median was ~0.12 ms early and ~0.37 ms over the full run, with late
iterations sometimes 2-15 ms. The cause was not isolated (thermal throttling and concurrent
system activity are plausible); no performance regression is claimed from this data and no
hardware counters were available. Raw log: `artifacts/soak.log` (ignored, not committed).

## 2026-11-26 — publication and hosted CI

Repository created and pushed to <https://github.com/timfromhcs/HCSUDA> (public, `main`).
No GitHub Release was created. CI initially failed on hosted runners for four real reasons,
each fixed and verified with a new run:

1. `windows-latest` had no Visual Studio → switched to `windows-2022`.
2. `scripts/check-secrets.ps1` had a hardcoded `D:\HCSUDA` path → derived from `$PSScriptRoot`.
3. CPU-only builds compiled the Vulkan-only IR test and failed to link generated SPIR-V symbols
   → test source is now conditional on `HCS_ENABLE_VULKAN`.
4. ModelVault compared a canonical model path against a non-canonical (junction) root → root is
   canonicalized in `initialize()`. Installer scripts had hardcoded paths and assumed a system
   Vulkan loader; both were corrected.

Final run: `45800355572`, conclusion `success`, all six jobs green. Node.js 20 deprecation
warnings from `actions/checkout@v4` are upstream annotations, not failures.

## 2026-11-26 — cross-version upgrade on the reference machine

Created a temporary git worktree at commit `b81457e` (the commit that introduced the native
setup), built its single-file EXE, installed it, then installed the current EXE over it using
`tests/installer/test_cross_version_upgrade.ps1`. Result: `CROSS_VERSION_UPGRADE_PASS` — a user
sentinel file survived, the upgraded `hcsdoctor --json` reported the real Radeon 680M and CPU
devices, and uninstall removed only HCSUDA files. The temporary worktree was removed afterwards.

This closes the cross-version upgrade item on the reference machine; a clean-VM test is still
not performed.

## 2026-11-26 — static analysis preset

Added `HCS_STATIC_ANALYSIS` and the `windows-analyze` preset (MSVC `/analyze`). First run found
one real issue: `Sha256::hashFile` used a 64 KiB stack buffer (C6262). Fixed by allocating it on
the heap; C4996 deprecation noise was handled with `_CRT_SECURE_NO_WARNINGS` for project code.
The second run produced zero warnings. Added a hosted CI job for the preset.

```
cmake --preset windows-analyze
cmake --build --preset windows-analyze --parallel  -> zero warnings
ctest --preset windows-tests --output-on-failure -> 81/81
```

## 2026-11-26 — HCSIR binary serialization (deferred Wave 4 item)

Added `serialize`/`deserialize` to `src/ir/hcsir.cpp` with a versioned 8-byte magic header,
explicit little-endian fields, strict count/name/index validation, canonical round-trip and
branch-target checks. Three tests were added (IR-21..23).

Two defects were found by the tests and fixed: `Module` has no `clear()` (now assigned a fresh
value), and deserialization initially rejected empty branch-target strings used by
non-control instructions (now read with an optional-text helper).

```
cmake --build --preset windows-tests --parallel
ctest --preset windows-tests --output-on-failure -> 84/84
ctest --preset windows-validation --timeout 180 -> 84/84, zero validation errors
cmake --build --preset windows-analyze --parallel -> zero warnings
```

## 2026-11-26 — CI found a flaky token test

Hosted CI failed `rest_requires_random_bearer_token` because the mutation `"0" + token.substr(1)`
is identical to the original token whenever the random hex token starts with `0`. Fixed by always
flipping the first character to the opposite hex value. Local and hosted runs now pass.

Note: hosted runners have no Vulkan SDK, so their `windows-tests` binary contains 81 tests (the
three HCSIR-generated-SPIR-V tests are correctly excluded); the reference machine runs 84.

## 2026-11-28 — Limitless Work loop started; truthful-copy gap closed

Reconnaissance: working tree clean on `main` (`fde331d` == `origin/main`); no other branches,
so there is no branch diff to merge. Wrote `docs/LIMITLESS_WORK_PLAN.md`, started a lock-protected
never-ending build/test driver (`scripts/loop-build-test.ps1`, log `logs/limitless-loop.log`) and
a 2-minute chat pulse automation.

First gap closed: stale claims in `docs/KNOWN_LIMITATIONS.md`, `README.md`,
`docs/release/RELEASE_NOTES.md` and the `hcsport` tool header contradicted implemented/tested
features (OpenAPI, settings UI+REST, vecadd translation, cross-version upgrade, HCSIR binary
serialization). Copy now matches `docs/TEST_MATRIX.md` and `docs/AGENT_STATUS.md`.

```
powershell -File scripts\build-test-once.ps1
configure exit=0; build exit=0; test exit=0
ctest --preset windows-tests --output-on-failure -> 1/1 CTest target, 84/84 HCS tests
```

The background loop has since completed further green cycles (e.g. 10:25:58, 10:26:29, 10:27:35,
all `configure/build/test exit=0`).

Further limitless iterations (same day):

* Added `ctest` presets for `windows-cpu-only`, `windows-release`, `windows-relwithdebinfo` and
  `windows-vulkan`; documented `ctest --preset windows-cpu-only`. Verified:
  `ctest --preset windows-cpu-only --output-on-failure` → 81/81, `windows-validation` → 84/84
  with zero validation errors. First CPU-only attempt using `ctest --config` failed with
  "Unknown argument: --config"; CTest uses `-C` or a test preset (now provided).
* Aligned release identity: binary/CMake version is `0.1.0`, so release notes, publication
  follow-up commands and remaining-tag wording now use `v0.1.0-rc.1` instead of `v1.0.0-rc.1`.
* Verified the documented CLI path `hcsport --convert tests/fixtures/cuda/vecadd.cu --output
  <temp>`: exit 0, valid `hcsir 1` module (PORT-04).
* Started `hcsbridge-rest --port 8712` with a local-only throwaway token, captured
  `docs/screenshots/ui-dashboard.png` with headless Chrome from the live page (real devices,
  memory budgets and settings), then terminated the service (UI-04).

No code defect was found in these iterations; all changes are documentation, presets and evidence.

Final verification of the limitless drive (2026-11-28):

```
ctest --preset windows-tests --output-on-failure -> 84/84 (background cycles 10:25-10:31 all green)
ctest --preset windows-cpu-only --output-on-failure -> 81/81
ctest --preset windows-validation --timeout 180 -> 84/84, zero validation errors
cmake --build --preset windows-analyze --parallel -> zero warnings
powershell -File scripts\check-secrets.ps1 -> SECRET_SCAN_PASS (118 tracked files)
```

Final status: internally closable work is complete; only owner/external release actions remain
(clean VM, signing certificate, approved GitHub release, cross-vendor hardware). Loop stopped.

## 2026-11-28 — Endless work restarted; automated UI capture (Build-Test-Fix)

Pushed the three earlier commits (`fde331d..ae0d5ff`), restarted the endless background loop and
the 2-minute chat pulse. Checked whether external blockers could be closed locally: there is no
code-signing certificate in `Cert:\CurrentUser\My`, no second Vulkan GPU, and the only local ISO is
a Linux image (`D:\AI\vmtest\dev2\...`), so a clean Windows 11 VM test remains impossible here.

Closed next internal gap: no automated UI screenshot regression. Added
`tests/ui/capture_ui.ps1` and CTest target `hcs_ui_capture` (free loopback port, throwaway token,
headless Chrome/Edge, PNG signature + size validation, SKIP without browser).

Failure found and fixed: first run failed because PowerShell 5.1 treats Chrome's benign stderr
registry warning as an error record even with `2>$null` (`FAIL: Missing value path for key ...`).
Replaced the direct invocation with `Start-Process -Wait` and redirected stdout/stderr to temp
files (UI-05).

```
ctest --preset windows-tests -R hcs_ui_capture -V -> PASS (52937 bytes)
powershell -File scripts\build-test-once.ps1 -> configure/build/test exit=0 (2/2 CTest targets)
cmake --preset windows-cpu-only; ctest --preset windows-cpu-only -> 2/2 PASS
```

## 2026-11-28 — Endless work iteration 2: REST rate limiting

Closed the documented gap "no rate limiting beyond bounded bodies" (AGENTS.md 11.2). Added a
process-wide fixed-window limiter to `src/api/rest/rest_server.{h,cpp}` (120 requests / 10 s,
HTTP 429 with `rate_limited`), wired the server process state in `tools/hcsbridge-rest/main.cpp`,
documented it in OpenAPI and added REST-06. Health remains exempt so monitoring stays available;
the pure `handleRequest` helper stays unlimited when no limiter is supplied (backward compatible).

Build-Test-Fix evidence:

1. First build failed with `LNK1168: cannot open hcsbridge-rest.exe for writing`. Root cause was a
   stale server process (PID 24664) holding the EXE, not a source defect. Killed it and hardened
   `tests/ui/capture_ui.ps1` to stop and wait for its own server and to clean up any leftover
   server pointing at the same binary.
2. `powershell -File scripts\build-test-once.ps1` → configure/build/test exit=0, 2/2 CTest targets,
   85/85 HCS tests.
3. `cmake --preset windows-cpu-only && ctest --preset windows-cpu-only` → 2/2 targets, 82/82 tests.
4. `cmake --preset windows-validation && ctest --preset windows-validation` → 2/2 targets, 85/85
   tests, zero Vulkan validation errors.

## 2026-11-28 — Endless work iteration 3: persistent Vulkan pipeline cache

Closed the deferred cache item without an ABI change. Added an internal cache-directory setter in
`src/core/runtime.cpp`, a bounded 80-byte wrapper format (magic / device-key / size) around
`vkGetPipelineCacheData` in `src/backends/vulkan/vk_backend.cpp`, and wired the existing
`shader_cache_dir` setting through `tools/hcsbridge-rest/main.cpp`. Cache files are keyed by
SHA-256 of vendor/device/driver/API version, pipelineCacheUUID, product version and a format tag;
corrupt, stale, truncated or >8 MiB files are ignored. Pipeline creation now passes the cache in
both the built-in and experimental-program paths.

Build-Test-Fix evidence:

1. First build failed: `C2198: PFN_vkGetPipelineCacheData too few arguments` (needs a fourth data
   pointer; the second call also wrote into the size variable). Fixed both calls.
2. Second build failed: `C2512: SettingsStore has no default constructor`. Fixed by passing an
   explicit empty `std::filesystem::path{}`.
3. Third build green. New VK-06 test confirmed the cache file is written and reloaded:
   `86 tests run, 0 failures` on `windows-tests`.
4. `windows-cpu-only`: 2/2 CTest targets, 83/83 HCS tests.
   `windows-validation`: 2/2 CTest targets, 86/86 HCS tests, zero validation errors.
   `windows-analyze`: zero warnings.

## 2026-11-28 — Endless work iteration 4: final reduction kernel

Added `reduce_final_f32` as a single-workgroup kernel: GLSL (`shaders/hksl/reduce_final.comp`,
compiled and embedded by `glslc`), CPU scalar implementation, kernel registry entry and SPIR-V
mapping. This completes the documented two-stage reduction without changing any ABI.

```
for i in retry(lock); powershell -File scripts\build-test-once.ps1  -> 0/0/0
& build\windows-tests\RelWithDebInfo\hcs_tests.exe -> 88 tests run, 0 failures
cmake --preset windows-cpu-only && ctest --preset windows-cpu-only -> 2/2, 85 tests
cmake --preset windows-validation && ctest --preset windows-validation -> 2/2, 88 tests,
zero validation errors
```

`reduce_final_cpu_matches_reference` and `reduce_final_vulkan_matches_reference` both pass with
37 partial sums and a 2e-4 relative tolerance (KC-07/KC-08).

## 2026-11-28 — Endless work iteration 6b: histogram kernel and stale-link finding

Added `histogram_u32` (256 bins, single workgroup, atomic increments) on CPU and Vulkan. The Vulkan
dispatch rejects `group_count_x != 1` so the bin-reset phase cannot race with increments.

Build-Test-Fix: the first run failed with `HCS_E_KERNEL_NOT_FOUND (no SPIR-V module for this
kernel)` on the Vulkan variant. A temporary diagnostic showed that after a full rebuild the blob
was present; cause was a stale link during a parallel build cycle, not a code defect. The
diagnostic print was removed and the suite re-run cleanly. This reinforces that a cycle must
complete a full configure/build before trusting its test result.

```
powershell -File scripts\build-test-once.ps1 -> 0/0/0; 92 tests run, 0 failures
cmake --preset windows-cpu-only && ctest --preset windows-cpu-only -> 2/2, 89 tests
cmake --preset windows-validation && ctest --preset windows-validation -> 2/2, 92 tests,
zero validation errors
```

## 2026-11-28 — Endless work iteration 7: loop hardening

The stale-link incident was addressed at the harness level: `scripts/build-test-once.ps1` now
builds the loop preset **sequentially** (no `--parallel`) and checks that `hcs_tests.exe` is not
older than `hcsuda_core.lib`; if it is, it relinks once before running tests. Manual profile builds
still use `--parallel` in separate build directories. Verified:

```
powershell -File scripts\build-test-once.ps1 -> configure/build/test exit=0; 2/2 CTest targets
```

Benchmark smoke after the kernel additions (real Radeon 680M, `--device 0 --reps 5 --warmup 2
--json`): vecadd median 0.2558 ms (verified), scale 0.2817 ms, reduce 0.1467 ms, matmul
0.1672 ms, scan 0.0949 ms, transpose 0.0979 ms. Only vecadd is marked `verified: true` by
`hcsbench`; the others are correctness-tested in the unit suite but not re-verified inside the
benchmark tool. These are wall-clock numbers including dispatch overhead — no pure-kernel-timing
claim is made.

## 2026-11-28 — Endless work iteration 8: full benchmark result validation

Added host-reference verification for scale, reduce, matmul, scan and transpose in
`tools/hcsbench/main.cpp`; previously only vecadd set `verified:true`.

Build-Test-Fix:

1. Build failed with `C2737: 'limit'/'allowed' const object must be initialized` because
   `std::max(1.0, std::fabs(float))` mixes double and float in template deduction. Fixed with an
   explicit `static_cast<double>`; also renamed shadowing loop indices.
2. After the build succeeded, five rows were verified but `scan_f32` reported a 19.5 worst-case
   mismatch. Root cause: the benchmark dispatched `groups=256` for `n=1,048,576`, while
   `scan_f32` handles at most 256 elements per workgroup chunk. Fixed the benchmark to use
   `groups=ceil(n/256)=4096`.
3. Final run: all six rows `verified:true` on the Radeon 680M; full unit suite still green
   (92/92, 2/2 CTest targets).

## 2026-11-28 — Endless work iteration 9: arbitrary-size scan chunks

The verified benchmark exposed a kernel limitation: `scan_f32` handled only 256 elements per
workgroup chunk, which had forced a benchmark workaround. Rewrote `shaders/hksl/scan.comp` to
iterate over 256-element blocks with a shared `running` prefix, added KC-13 (n=10000, groups=4 →
2500-element chunks) on CPU and Vulkan, and reverted the benchmark to `groups=256`.

```
for i in retry(lock); powershell -File scripts\build-test-once.ps1 -> 0/0/0; 93 tests run, 0 failures
cmake --preset windows-cpu-only && ctest --preset windows-cpu-only -> 2/2, 90 tests
cmake --preset windows-validation && ctest --preset windows-validation -> 2/2, 93 tests,
zero validation errors
hcsbench --device 0 --reps 5 --warmup 2 --json -> all six rows verified:true,
scan_f32 n=1048576 g=256 median 0.4535 ms
```

Also removed the last C4456 shadowing warning in `tools/hcsbench/main.cpp`.

## 2026-11-28 — Endless work iteration 10: release-gate re-certification

Re-ran release-critical gates after the shader/benchmark changes:

```
powershell -File tests\installer\test_installer.ps1 -> INSTALLER_TEST_PASS
powershell -File tests\installer\test_upgrade.ps1   -> UPGRADE_TEST_PASS
powershell -File scripts\check-secrets.ps1          -> SECRET_SCAN_PASS (122 tracked files)
cmake --build --preset windows-analyze --parallel   -> zero warnings
```

Install/uninstall and upgrade still preserve user data and leave the Vulkan loader intact.

## 2026-11-28 — Endless work iteration 11: reproducible soak harness

The earlier soak was a one-off run; added `scripts/soak.ps1` as a bounded, reproducible harness:
fixed small workloads, one device, JSON verification of every benchmark row, per-iteration log and
a final summary JSON under `logs/`. No unbounded allocations or sleeps beyond the 2 s interval.

```
powershell -File scripts\soak.ps1 -Minutes 1 -Device 0
-> SOAK PASS: iterations=24 failed_runs=0 unverified_rows=0
```

A 60-minute run was started in the background afterwards; its summary will be recorded in
`docs/TEST_MATRIX.md` (SOAK-02) when it completes.

## 2026-11-28 — Endless work iteration 12: safe MCP model listing

Added the fifth read-only MCP tool `hcsuda_list_models` in `src/api/mcp/mcp_server.cpp`. It reads
the configured model catalog via `ModelVault::list` and returns only id, name, format, size,
hash flag and `runnable:false` — no filesystem paths, file contents or downloads.

Test fix: MCP-07 initially asserted a `runnable` field, but the unconfigured catalog correctly
returns an empty model list; replaced with a no-path-leak assertion. Result: 94/94 tests.

## 2026-11-28 — Endless work iteration 13: safe REST model listing

Added authenticated `GET /api/v1/models` to `src/api/rest/rest_server.cpp` with the same safe
metadata shape as the MCP tool and no paths/file contents. Documented in `docs/api/openapi.json`.

Build-Test-Fix: first build failed with `C2039: escape is not a member of hcs::mcp` because the MCP
escape helper is file-local. Added a local `escapeJson` in the REST handler and rebuilt:
`95 tests run, 0 failures`.

## 2026-11-28 — Endless work iteration 14: real UI model section

Connected the new endpoint to the self-contained console: `src/api/rest/ui_page.cpp` now has a
Models section calling `/api/v1/models`, with an explicit note that HDD is storage and no inference
adapter exists in V1. UI-01 asserts the section and endpoint are present. No fake controls were
added; the table is populated only from the live service.

```
powershell -File scripts\build-test-once.ps1 -> configure/build/test exit=0; 95 tests, 0 failures
```

The README screenshot was recaptured from the live service at the updated UI (Models section
visible, empty catalog shown honestly as "No data.").

## 2026-11-28 — Endless work iteration 15: UI output escaping

The console inserted catalog values and the configured model directory straight into HTML. Added a
`esc()` helper and used it for table cells and the model-dir note so user-controlled catalog data
cannot inject markup. UI-01 asserts the helper exists. A build then hit `LNK1168` again: the manual
screenshot server had survived as a child process of the shell wrapper (PID 3572) and held the EXE.
After terminating it, configure/build/test were green with 95/95 tests. No service is left running.

Documentation consistency pass: updated stale "four read-only tools" wording in `docs/api/mcp.md`,
`docs/KNOWN_LIMITATIONS.md` and `docs/release/v1-scope.md` to five tools, and added the new
`hcsuda_list_models` tool description. Live REST smoke on port 8714 (process started and stopped in
the same command): health=ok, `models.configured=false`, `models.count=0`, unauthenticated=401.

## 2026-11-28 — Endless work iteration 16: cache option for hcsbench

Added `--cache-dir` to `hcsbench`, wired to the internal pipeline-cache setter before `hcsInit`,
and gave the target its own `src` include path. Verified with a temporary directory: a real
device-keyed cache blob (16,260 bytes) was written. Full suite still green (95/95, 2/2 CTest
targets).

Final gate sweep after the UI/API additions:

```
Get-Content docs\api\openapi.json | ConvertFrom-Json -> openapi=3.1.0, 9 paths, /api/v1/models present
powershell -File tests\installer\test_installer.ps1 -> INSTALLER_TEST_PASS
powershell -File tests\installer\test_upgrade.ps1   -> UPGRADE_TEST_PASS
powershell -File scripts\check-secrets.ps1          -> SECRET_SCAN_PASS (123 tracked files)
```

Added `scripts/check-openapi.ps1` as a reproducible contract gate:

```
powershell -File scripts\check-openapi.ps1 -> OPENAPI_PASS (9 paths, 8 required endpoints)
```

Final sweep after the documentation scope decision:

```
cmake --build --preset windows-analyze --parallel -> zero warnings
ctest --preset windows-tests --output-on-failure -> 2/2 CTest targets, 95/95 HCS tests
powershell -File scripts\check-openapi.ps1 -> OPENAPI_PASS (9 paths, 8 required endpoints)
```

## 2026-11-28 — Endless work iteration 6: host-free global scan

Added `scan_add_offsets_f32` (GLSL + CPU + registry + SPIR-V mapping) so a global exclusive scan
can run entirely on the selected backend: local scan → chunk totals → scan offsets → add offsets.
Test bug found and fixed: the totals stage initially reduced the first `groups` input elements
instead of the chunk sums; corrected to `n=10001, groups=64` on the full input.

```
powershell -File scripts\build-test-once.ps1 (after fix) -> 0/0/0
& build\windows-tests\RelWithDebInfo\hcs_tests.exe -> 90 tests run, 0 failures
cmake --preset windows-cpu-only && ctest --preset windows-cpu-only -> 2/2, 87 tests
cmake --preset windows-validation && ctest --preset windows-validation -> 2/2, 90 tests,
zero validation errors
```

## 2026-11-28 — Endless work iteration 5: installer/security re-certification

Re-ran the release-critical gates after the cache, rate-limit and reduce changes:

```
powershell -File tests\installer\test_installer.ps1 -> INSTALLER_TEST_PASS
powershell -File tests\installer\test_upgrade.ps1   -> UPGRADE_TEST_PASS
powershell -File scripts\check-secrets.ps1          -> SECRET_SCAN_PASS (120 tracked files)
```

Install/uninstall still preserves user data and leaves the system Vulkan loader untouched.
