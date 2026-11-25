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
