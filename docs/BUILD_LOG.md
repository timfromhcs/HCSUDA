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
