#pragma once

/*
 * HCSIR v1 — HCSUDA portable intermediate representation.
 *
 * Line-oriented, bounded, verifiable text format. This is an internal
 * implementation used by the CPU interpreter and (later) the GLSL lowering
 * pass. It is not yet exposed through the stable C ABI.
 *
 * See docs/architecture/ADR-0004-hcsir.md.
 */

#include "hcsuda/hcsuda.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hcs::ir {

constexpr uint32_t kMaxSourceBytes = 1u << 20; /* 1 MiB */
constexpr uint32_t kMaxInstructions = 4096;
constexpr uint32_t kMaxValues = 256;
constexpr uint32_t kMaxBlocks = 64;
constexpr uint32_t kMaxBuffers = 8;
constexpr uint32_t kMaxParams = 16;
constexpr uint32_t kMaxIdentifier = 128;
constexpr uint32_t kMaxStepsPerInvocation = 100000;
constexpr uint32_t kVersion = 1;

enum class Type { Void = 0, F32, U32, I32, Bool };

enum class Op {
    Invocation,
    Load,
    Store,
    Add,
    Sub,
    Mul,
    Fma,
    Min,
    Max,
    CmpLt,
    CmpLe,
    CmpGt,
    CmpGe,
    CmpEq,
    CmpNe,
    Select,
    Branch,
    Jump,
    Barrier,
    Ret
};

struct Operand {
    enum class Kind { None, Value, FloatLit, IntLit } kind = Kind::None;
    uint32_t id = 0;
    double number = 0.0;
};

struct Instruction {
    Op op = Op::Ret;
    int dst = -1;
    Operand a;
    Operand b;
    Operand c;
    int buffer = -1;
    std::string targetTrue;
    std::string targetFalse;
    uint32_t line = 0;
};

struct Block {
    std::string name;
    std::vector<Instruction> code;
    uint32_t line = 0;
};

struct BufferDecl {
    uint32_t index = 0;
    std::string name;
    Type elemType = Type::F32;
};

struct ParamDecl {
    std::string name;
    Type type = Type::F32;
    uint32_t valueId = 0;
};

struct ConstantDecl {
    std::string name;
    Type type = Type::F32;
    double value = 0.0;
    uint32_t valueId = 0;
};

struct Module {
    std::string kernel;
    std::vector<BufferDecl> buffers;
    std::vector<ParamDecl> params;
    std::vector<ConstantDecl> constants;
    std::vector<Block> blocks;
    std::vector<Type> valueTypes;
    uint32_t localSize = 0;
    uint32_t instructionCount = 0;
};

struct RunArgs {
    void* buffers[kMaxBuffers]{};
    uint64_t sizes[kMaxBuffers]{};
    uint32_t bufferCount = 0;
    double params[kMaxParams]{};
    uint32_t paramCount = 0;
};

/* Parses and verifies a module. On failure the last-error carries line info. */
hcsResult compile(const char* source, size_t size, Module* out);

/* Executes a verified module for `invocationCount` invocations. */
hcsResult run(const Module& module, const RunArgs& args, uint32_t invocationCount);

/* Lowers a verified module to a GLSL 450 compute shader (elementwise subset only).
 * Returns HCS_E_UNSUPPORTED_FEATURE for constructs the lowering cannot express safely,
 * such as a barrier in divergent control flow. */
hcsResult lowerGlsl(const Module& module, std::string* out);

/* Versioned binary serialization with bounded input and canonical round-trip.
 * deserialize() validates every length/count/enum and all branch targets before returning. */
hcsResult serialize(const Module& module, std::string* out);
hcsResult deserialize(const std::string& bytes, Module* out);

const char* opName(Op op);
const char* typeName(Type type);

} // namespace hcs::ir
