# ADR-0004: HCSIR v1 text format, verification and threat model

* Status: accepted (design), implementation pending Wave 4
* Date: 2026-11-25

## Context

HCSUDA needs its own portable IR (HCSIR) with a virtual instruction vocabulary (HCS-PIR).
It must be verifiable before execution, versioned, and safe against hostile input. It is not
a claim of native hardware instructions.

## Decision: format

HCSIR v1 is a line-oriented text format with a strict grammar:

```
hcsir 1
kernel vecadd_f32
param f32* %a
param f32* %b
param f32* %c
const u32 %n
op HLOAD.F32 %a, %i -> %va
op HFMA.F32 %alpha, %vb, %va -> %sum
op HSTORE.F32 %c, %i, %sum
end
```

Properties:

* First line must be `hcsir 1`; unknown versions are rejected with
  `HCS_E_UNSUPPORTED_API_VERSION`.
* Exactly one kernel per module in v1.
* Identifiers are ASCII `[A-Za-z_][A-Za-z0-9_.]*`, max 128 bytes.
* Maximum module size 1 MiB, maximum 4096 instructions, maximum 256 live values.
* No include, no macro, no external file reference, no host pointer arithmetic.
* Parse errors report line and column; they never partially execute.

## Decision: semantics

* Types: `f32`, `i32`, `u32`, `bool`, plus pointers to these in global memory.
* Arithmetic is IEEE-754 binary32 with round-to-nearest-even. NaN propagates; signed zero is
  preserved. Denormals follow the backend; a backend that flushes must declare it.
* Integer overflow wraps (two's complement) for `u32`/`i32` unless an opcode says otherwise.
* Memory: `HLOAD`/`HSTORE` are bounds-checked against the buffer size; out-of-range access is
  a runtime error (`HCS_E_INVALID_ARGUMENT`), not undefined behavior.
* `HBARRIER.WORKGROUP` maps to a workgroup barrier; `HMEMBAR.DEVICE` is only valid when the
  backend declares a matching memory model.
* Atomics are only accepted when the device capability bit is present; otherwise the verifier
  fails with `HCS_E_UNSUPPORTED_FEATURE` at compile time, not at dispatch time.

## Verification passes

1. Lexical and grammatical validation with bounded recursion and token count.
2. Type checking of every operand and result.
3. Definite-assignment analysis for values.
4. Control-flow validation: every block terminated, no unreachable fallthrough, loop nesting
   limit.
5. Memory access validation against declared buffers.
6. Capability gating for atomics, subgroup and matrix operations.
7. Deterministic serialization round-trip check (serialize → parse → compare).

## Threat model

Untrusted input: HCSIR modules, later PTX and CUDA source. Protections:

* Bounded memory, token count, nesting depth, instruction count and compile time.
* No file, network, or process access from the parser or verifier.
* Errors are structured and bounded; no raw dumps to the UI.
* Fuzzing targets the parser and verifier (Wave 12).

## Consequences

* The interpreter backend (Wave 4) gives a third correctness reference beside the scalar CPU
  backend and Vulkan.
* Lowering HCSIR to GLSL is a separate, testable pass; the hand-written kernels remain the
  conformance baseline until the generated path matches them.
