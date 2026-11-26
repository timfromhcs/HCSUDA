# Test matrix

Environment: Windows 11 Pro 10.0.26200, Ryzen 7 7735HS, Radeon 680M (UMA),
AMD driver 32.0.21043.12001, Vulkan 1.4.315, MSVC 19.44 (RelWithDebInfo).

Run: `ctest --preset windows-tests --output-on-failure`
Last result: **1/1 CTest target passed; 81/81 HCS tests passed** (2026-11-25).

| ID | Test | Backend | Result | What it proves |
|---|---|---|---|---|
| API-01 | version_identity | — | PASS | ABI version, error strings, unknown-code fallback |
| API-02 | init_requires_once | — | PASS | init/shutdown lifecycle, double-init rejected, idempotent shutdown |
| API-03 | enumerate_devices_reports_real_hardware | both | PASS | ≥1 device, CPU always present, non-empty names, non-zero ids |
| API-04 | invalid_arguments_are_rejected | both | PASS | null checks, caps query, base-compute bit |
| API-05 | kernel_lookup_is_strict | CPU | PASS | unknown kernel → `HCS_E_KERNEL_NOT_FOUND`, message + correlation id |
| API-06 | buffer_bounds_are_checked | CPU | PASS | upload/download/clear range validation |
| API-07 | dispatch_requires_matching_context | CPU | PASS | ABI size check, zero group count rejected, buffer arity |
| CPU-01 | cpu_vecadd_matches_reference | CPU | PASS | 1000 elements (non-multiple of 64), alpha 2.5, rel 1e-5 |
| CPU-02 | cpu_scale_matches_reference | CPU | PASS | 4096 elements, negative alpha |
| CPU-03 | cpu_reduce_matches_reference | CPU | PASS | 8 groups over 10000 elements, rel 1e-4 |
| CPU-04 | cpu_matmul_matches_reference | CPU | PASS | 37×31×29 odd dimensions, rel 1e-5 |
| CPU-05 | cpu_vecadd_large_parallel_run | CPU | PASS | 1 Mi element, thread-parallel AVX2 path |
| CPU-06 | cpu_rejects_undersized_buffers | CPU | PASS | no out-of-bounds writes, clamped to smallest bound |
| VK-01 | vulkan_device_report_is_truthful | Vulkan | PASS | caps sane; heap 9569.9 MiB, budget 9091.4 MiB reported by driver |
| VK-02 | vulkan_vecadd_matches_cpu_reference | Vulkan | PASS | 65536 elements vs double reference, rel 1e-5 |
| VK-03 | vulkan_reduce_matches_cpu_reference | Vulkan | PASS | 100000 elements, 64 groups, rel 2e-4 |
| VK-04 | vulkan_matmul_matches_cpu_reference | Vulkan | PASS | 64³ tiled matmul vs double reference, rel 1e-4 |
| VK-05 | vulkan_memory_cap_is_enforced | Vulkan | PASS | 4 MiB cap rejects 16 MiB allocation with `HCS_E_OUT_OF_MEMORY` |
| IR-01 | ir_vecadd_matches_reference | interpreter | PASS | 1000 invocations (not a multiple of `local_size`) match a double reference, rel 1e-5 |
| IR-02 | ir_branch_and_select_execute | interpreter | PASS | `gt` + `branch` + constants; 3 blocks execute in source order, 257 invocations |
| IR-03 | ir_rejects_unknown_version | interpreter | PASS | `hcsir 2` → `HCS_E_UNSUPPORTED_API_VERSION`, diagnostic carries line 1 |
| IR-04 | ir_rejects_unknown_opcode | interpreter | PASS | unknown opcode → `HCS_E_UNSUPPORTED_FEATURE` |
| IR-05 | ir_rejects_undefined_value | interpreter | PASS | undefined operand → `HCS_E_INVALID_ARGUMENT` |
| IR-06 | ir_rejects_type_mismatch | interpreter | PASS | `add f32, u32` rejected; no implicit conversion |
| IR-07 | ir_rejects_missing_terminator | interpreter | PASS | block without terminator rejected |
| IR-08 | ir_rejects_unknown_branch_target | interpreter | PASS | branch to non-existent label rejected |
| IR-09 | ir_rejects_oversized_source | interpreter | PASS | >1 MiB source → `HCS_E_SECURITY_POLICY` |
| IR-10 | ir_rejects_out_of_bounds_access | interpreter | PASS | 1024 invocations over 8 elements → `HCS_E_INVALID_ARGUMENT`, no OOB access |
| IR-11 | ir_rejects_unbound_buffer | interpreter | PASS | missing buffer binding rejected |
| IR-12 | ir_rejects_zero_and_huge_invocation_counts | interpreter | PASS | 0 is a no-op; 4M+1 → `HCS_E_SECURITY_POLICY` |
| IR-13 | ir_glsl_lowering_emits_compute_shader | lowering | PASS | emitted GLSL has the right local size, buffers, push constants, bounds checks and intrinsics |
| IR-14 | ir_glsl_lowering_is_deterministic | lowering | PASS | two lowerings of the same module are byte-identical |
| IR-15 | ir_glsl_lowering_rejects_barrier | lowering | PASS | barrier → `HCS_E_UNSUPPORTED_FEATURE`, no partial output |
| IR-16 | ir_glsl_lowering_rejects_uncompiled_module | lowering | PASS | uncompiled module / null output rejected |
| IR-17 | build-time generated shader compiles | `glslc` | PASS | `hcscc` lowers `tests/fixtures/ir/vecadd.hcsir`; `glslc --target-env=vulkan1.1 -O` produces SPIR-V during `cmake --build` |
| IR-18 | hcsir_generated_spirv_runs_on_vulkan | Vulkan | PASS | HCSCC-generated SPIR-V loaded via the experimental program API; 4096 invocations match the HCSIR reference within 2e-5 |
| IR-19 | hcsir_program_rejects_invalid_spirv | Vulkan | PASS | bad magic → `HCS_E_SPIRV_INVALID`; unaligned/null input → `HCS_E_INVALID_ARGUMENT` |
| IR-20 | hcsir_program_is_unsupported_on_cpu_backend | CPU | PASS | CPU returns `HCS_E_UNSUPPORTED_FEATURE` instead of pretending to decode SPIR-V |
| FL-01 | flow_async_dispatches_and_event_wait | Vulkan | PASS | 8 async dispatches on one flow; event wait observes fence status before explicit sync; final output matches |
| FL-02 | flow_two_flows_are_independent | Vulkan | PASS | two flows with different alphas, synchronized in reverse order, both results correct |
| FL-03 | flow_event_timeout_and_validation | Vulkan | PASS | idle-flow event is immediately satisfied; null flow/event misuse rejected |
| FL-04 | flow_cpu_backend_completes_immediately | CPU | PASS | CPU flow/event complete synchronously with zero timeout |
| FL-05 | flow_event_wait_timeout_path | mock flow | PASS | a backend flow whose counter never advances yields `HCS_E_TIMEOUT` |
| VAL-01 | full unit suite under validation layers | Vulkan + validation | PASS | `VK_LAYER_KHRONOS_validation` enabled explicitly; 81/81 tests, no validation errors |
| GR-01 | graph_runs_on_cpu_backend | CPU | PASS | 2-node linear DAG with dependency, replayed on a flow, both outputs verified |
| GR-02 | graph_runs_on_vulkan_backend | Vulkan | PASS | same DAG executes on the Radeon 680M and matches the CPU result |
| GR-03 | graph_rejects_invalid_dependencies | CPU | PASS | empty graph, future dependency, zero group count, >8 dependencies all rejected |
| GR-04 | graph_add_node_validates_context | CPU | PASS | null kernel/dispatch and null graph rejected |
| KC-01 | scan_cpu_odd_size_matches_reference | CPU | PASS | n=10000, 64 chunks: exclusive prefix per chunk |
| KC-02 | scan_vulkan_odd_size_matches_reference | Vulkan | PASS | same input/shape on Radeon 680M matches the reference after the lane-mask fix |
| KC-03 | scan_single_element | both | PASS | n=1, groups=1 |
| KC-04 | transpose_cpu_odd_dimensions | CPU | PASS | 37x29 row-major transpose |
| KC-05 | transpose_vulkan_odd_dimensions | Vulkan | PASS | same odd dimensions on the GPU |
| KC-06 | transpose_single_element_and_tile_edge | both | PASS | 1x1 and 17x16 tile boundary |
| PORT-01 | hcsport_scan_reports_constructs_without_claiming_translation | scanner | PASS | line/column findings, manual vs unsupported, JSON states `translated:0` |
| PORT-02 | hcsport_rejects_binary_and_oversized_input | scanner | PASS | NUL bytes, >4 MiB, empty and null inputs rejected |
| PORT-03 | hcsport_plain_host_code_has_no_findings | scanner | PASS | plain C++ produces an empty report |
| MV-01 | sha256_matches_known_vectors | model store | PASS | empty input, "abc", and split updates match FIPS vectors |
| MV-02 | model_vault_imports_and_persists_gguf | model store | PASS | GGUF v3 import, q4_k_m hint, SHA-256, persistence, removal |
| MV-03 | model_vault_rejects_unsafe_and_corrupt_inputs | model store | PASS | outside-root path, missing file, corrupt GGUF version, unknown format |
| MV-04 | model_vault_scan_detects_changes | model store | PASS | checksum and note updated after the file changes |
| MCP-01 | mcp_initialize_negotiates_protocol | MCP | PASS | protocol 2025-11-25, tools capability, server info |
| MCP-02 | mcp_notification_has_no_reply | MCP | PASS | notifications/initialized produces no stdout message |
| MCP-03 | mcp_tools_list_exposes_only_safe_read_only_tools | MCP | PASS | four read-only tools; no shell/file/download tool |
| MCP-04 | mcp_tools_call_returns_real_runtime_data | MCP | PASS | status, device list and memory budgets from the live runtime |
| MCP-05 | mcp_rejects_unknown_methods_and_tools | MCP | PASS | -32601 for unknown method and unknown tool |
| MCP-06 | mcp_rejects_malformed_and_oversized_messages | MCP | PASS | -32600 for empty, missing method, deep nesting and >1 MiB |
| REST-01 | rest_health_is_public_and_local_only | REST handler | PASS | public health, nosniff header |
| REST-02 | rest_requires_random_bearer_token | REST handler | PASS | 64-hex unique token, constant-time compare, 401 on bad/missing token |
| REST-03 | rest_rejects_cross_origin_oversized_and_traversal | REST handler | PASS | 403 cross-origin, 413 >64 KiB, 400 traversal, 404 unknown routes |
| REST-04 | rest_returns_real_runtime_data | REST handler | PASS | device list and memory budget from live runtime |
| REST-05 | live loopback request (manual) | REST server | PASS | health=200, unauthorized=401, devices=200 with Radeon 680M + CPU |
| TR-01 | translate_vecadd_subset_produces_runnable_hcsir | translator | PASS | fixture → HCSIR → compile → interpreter over 1001 elements matches CPU reference |
| TR-02 | translate_fma_variant_uses_fma_opcode | translator | PASS | scalar-factor variant emits `fma` |
| TR-03 | translate_fails_closed_on_unsupported_constructs | translator | PASS | atomics/no-kernel/null input rejected with no partial output |
| UI-01 | rest_serves_self_contained_local_ui | UI | PASS | self-contained HTML with live data calls, no remote resources, explicit unavailable state |
| UI-02 | live browser UI fetch (manual) | UI + REST | PASS | GET /ui/ -> 200 text/html, 4381 bytes from the running service |
| INS-01 | tests/installer/test_installer.ps1 | installer | PASS | staged install, file checks, installed hcsdoctor --json, uninstall, Vulkan loader intact |
| INS-02 | full single-EXE setup run (manual) | installer | PASS | SETUP_EXIT=0, real device report from installed binary, uninstall preserved user data |
| INS-03 | tests/installer/test_upgrade.ps1 | installer | PASS | in-place reinstall keeps user files, binaries still run, uninstall removes only HCSUDA files |
| SEC-01 | scripts/check-secrets.ps1 | repository | PASS | 104 tracked files scanned, no credential patterns in code (dummy tokens only in docs/tests) |
| SOAK-01 | 60-minute bounded soak | Vulkan | PASS (stability) | 1610 iterations, 0 failures; all vecadd samples verified. Median vecadd drifted 0.12 ms -> 0.37 ms with outliers to 15.5 ms; cause not diagnosed (thermal/system load), so no performance claim is made |
| SET-01 | settings_defaults_are_safe | settings | PASS | safe defaults: auto backend, no cap, port 8710, no telemetry/start-on-login |
| SET-02 | settings_persist_and_reset | settings | PASS | JSON persist/reload for 5 fields plus reset to defaults |
| SET-03 | settings_reject_invalid_values | settings | PASS | invalid policy/port/threads/theme/prefetch and traversal paths rejected atomically |
| SET-04 | settings_recover_from_corrupt_file | settings | PASS | corrupt stored settings → error + defaults |
| SET-05 | rest_settings_roundtrip_and_validation | REST | PASS | authenticated GET/POST/reset with 400 on invalid values |
| UI-03 | settings controls in local UI | UI | PASS | Save/Reset buttons wired to the real settings endpoints |
| STR-01 | stress_repeated_cpu_dispatch_reuses_buffers | CPU | PASS | 2000 synchronous dispatches reusing the same buffers |
| STR-02 | stress_async_flow_many_submissions | Vulkan | PASS | 500 async submissions on one flow, chunked fence wait, verified output |
| STR-03 | fuzz_bounded_parsers_reject_random_input | parsers/API | PASS | 200 seeded random inputs through HCSIR, scanner, MCP and REST handlers |

## Not yet covered (explicit gaps)

| Area | Status | Planned wave |
|---|---|---|
| HCSIR parser/verifier/interpreter | **tested** (IR-01..IR-12) | 4 |
| Async flows, events and linear HCSGraph DAGs | **tested** (FL-01..05, GR-01..04) | 5 |
| Scan and transpose | **tested** (KC-01..KC-06) | 6 |
| CUDA source scan + narrow vecadd translation | **tested** (PORT-01..03, TR-01..03) | 7 |
| PTX textual importer | not implemented | 7 (experimental) |
| Model catalog / checksum / path safety | **tested** (MV-01..04); no inference adapter | 8 |
| MCP stdio protocol conformance | **tested** (MCP-01..06) | 9 |
| Browser UI served locally | **tested** (UI-01..02); no automated screenshot capture yet | 10 |
| Installer staged + reference-machine install/uninstall + in-place upgrade | **tested** (INS-01..03); clean VM not tested | 11 |
| Stress/fuzz (2000 CPU, 500 GPU submissions, 200 fuzz inputs) + 60-min soak | **tested** (STR-01..03, SOAK-01) | 12 |
| Intel/NVIDIA Vulkan hardware | **not verified** | requires hardware |
| Vulkan validation layers run | **PASS** via `ctest --preset windows-validation --timeout 180` (52/52 tests, `VK_LAYER_KHRONOS_validation` explicitly enabled) | 5 |

## Benchmark evidence

Commands (exact):

```
build\windows-tests\RelWithDebInfo\hcsbench.exe --device 0 --reps 25 --warmup 5
build\windows-tests\RelWithDebInfo\hcsbench.exe --device 1 --reps 25 --warmup 5
```

Results: see `README.md` and `benchmarks/baselines/radeon680m-2026-11-25.json`.
Vecadd samples are verified against a host double-precision reference on every run.
