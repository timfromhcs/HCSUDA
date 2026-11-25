# HCSIR v1 — implemented grammar and limits

This document describes the format that `src/ir/hcsir.cpp` actually accepts, not a plan.
ADR-0004 records the design rationale; where the two differ, this file is authoritative
because it is checked against the test suite (`ir_*` tests).

HCSIR v1 is not exposed through the stable C ABI yet. It is an internal, verifiable
intermediate representation used by the CPU interpreter. There is **no** HCSIR → SPIR-V or
HCSIR → GLSL lowering in this build; the Vulkan path still runs the hand-written kernels in
`shaders/hksl/`.

## Example

```
hcsir 1
kernel ir_vecadd
buffer 0 a : f32
buffer 1 b : f32
buffer 2 c : f32
param alpha : f32
const zero : f32 = 0.0
local_size 64

entry:
  idx = invocation
  va = load a, idx
  vb = load b, idx
  sum = fma alpha, vb, va
  store c, idx, sum
  ret
```

## Declarations (must precede the first block)

| Form | Meaning |
|---|---|
| `hcsir 1` | First non-comment line. Any other version → `HCS_E_UNSUPPORTED_API_VERSION`. |
| `kernel <name>` | Exactly one per module. |
| `buffer <index> <name> : f32` | Storage buffer, `index` 0..7. Only `f32` is accepted in v1. |
| `param <name> : <type>` | Scalar parameter, `f32`, `u32` or `i32`. Maximum 16. |
| `const <name> : <type> = <literal>` | Named constant, same type set as parameters. |
| `local_size <n>` | Required, 1..1024. No implicit default. |

Duplicate names, duplicate buffer indices, and unknown declarations are rejected.

## Blocks

A label line is `<name>:` at column 0 after trimming. The first block is the entry block.
Every block must end with a terminator (`ret`, `jump`, `branch`); instructions after a
terminator are a compile error, as is an empty block. Branch targets must exist.

## Instructions

`<dst> = <op> ...` defines a value; `store`, `jump`, `branch`, `barrier` and `ret` have no
destination.

| Instruction | Types | Semantics |
|---|---|---|
| `idx = invocation` | `u32` | Linear invocation index in `[0, count)`. |
| `v = load <buffer>, <index>` | index integer, result `f32` | Bounds-checked read. |
| `store <buffer>, <index>, <value>` | index integer, value `f32` | Bounds-checked write. |
| `d = add a, b` | `f32` | `a + b` |
| `d = sub a, b` | `f32` | `a - b` |
| `d = mul a, b` | `f32` | `a * b` |
| `d = fma a, b, c` | `f32` | `a * b + c` |
| `d = min a, b` / `d = max a, b` | `f32` | IEEE `fmin`/`fmax` semantics |
| `d = lt\|le\|gt\|ge\|eq\|ne a, b` | same numeric type | Result `bool`. |
| `d = select cond, a, b` | `bool`, numeric `a`/`b` of equal type | `cond ? a : b` |
| `branch cond, <trueLabel>, <falseLabel>` | `bool` | Conditional jump. |
| `jump <label>` | — | Unconditional jump. |
| `barrier` | — | Accepted; no-op in the single-threaded interpreter. |
| `ret` | — | Ends the invocation. |

Operands may be values or numeric literals. Mixed `f32`/`i32` arithmetic is rejected, not
implicitly converted. Reduction of `f32` arithmetic is done in double precision inside the
interpreter and narrowed on store; the interpreter is a correctness reference, not a
performance target.

## Hard limits

| Limit | Value | Error |
|---|---|---|
| Source bytes | 1 MiB | `HCS_E_SECURITY_POLICY` |
| Instructions | 4096 | `HCS_E_SECURITY_POLICY` |
| Values | 256 | `HCS_E_SECURITY_POLICY` |
| Blocks | 64 | `HCS_E_SECURITY_POLICY` |
| Parameters | 16 | `HCS_E_SECURITY_POLICY` |
| Identifier length | 128 bytes | `HCS_E_INVALID_ARGUMENT` |
| Interpreted steps per invocation | 100 000 | `HCS_E_SECURITY_POLICY` |
| Invocations per run | 4 194 304 | `HCS_E_SECURITY_POLICY` |

Bounds violations at run time return `HCS_E_INVALID_ARGUMENT` and never touch memory outside
the bound buffer. Diagnostics include the original source line (`HCSIR line N: ...`), which is
why comment and blank lines are tracked by line number rather than by filtered index.

## Verified behaviour (tests)

`tests/unit/test_ir.cpp` covers: successful vecadd execution against a double-precision
reference with a non-multiple-of-local-size length, branch/constant execution, unknown
version, unknown opcode, undefined value, type mismatch, missing terminator, unknown branch
target, oversized source, out-of-bounds access, unbound buffer, and the invocation-count
bounds including the zero-invocation no-op.

## Not implemented (do not claim)

* Lowering HCSIR to GLSL/SPIR-V, so HCSIR modules cannot run on the GPU yet.
* Integer arithmetic opcodes, shifts, bitwise operations.
* Atomics, subgroup operations, matrix/tile operations.
* Loops as structured opcodes (`jump`/`branch` only).
* Serialization of a compiled module (there is no binary or stable transport format yet).
