#include "test_framework.h"

#include "ir/hcsir.h"

#include <cstring>
#include <string>

namespace {

using hcs::ir::Module;

const char* kVecAdd = R"(hcsir 1
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
  positive = gt vb, zero
  picked = select positive, va, vb
  sum = fma alpha, vb, picked
  store c, idx, sum
  ret
)";

Module compileOrDie(const char* text)
{
    Module module;
    const hcsResult result = hcs::ir::compile(text, std::strlen(text), &module);
    if (result != HCS_SUCCESS) {
        hcsErrorInfo info{};
        info.struct_size = sizeof(info);
        hcsGetLastError(&info);
        char buffer[512];
        std::snprintf(buffer, sizeof(buffer), "compile failed: %s", info.message);
        ::hcstest::reportFailure(__FILE__, __LINE__, "hcs::ir::compile", buffer);
    }
    return module;
}

bool contains(const std::string& text, const char* needle)
{
    return text.find(needle) != std::string::npos;
}

std::string lastMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    return info.message;
}

} // namespace

HCS_TEST(ir_glsl_lowering_emits_compute_shader)
{
    Module module = compileOrDie(kVecAdd);
    std::string glsl;
    HCS_REQUIRE_OK(hcs::ir::lowerGlsl(module, &glsl));

    CHECK_MSG(glsl.rfind("#version 450\n", 0) == 0, "shader must start with the version directive");
    CHECK_MSG(contains(glsl, "layout(local_size_x = 64) in;"), "local size must come from the module");
    CHECK_MSG(contains(glsl, "buffer HcsBuf0"), "three storage buffers expected");
    CHECK_MSG(contains(glsl, "buffer HcsBuf2"), "third buffer missing");
    CHECK_MSG(contains(glsl, "layout(push_constant) uniform HcsParams"), "parameters must be push constants");
    CHECK_MSG(contains(glsl, "void main()"), "entry point missing");
    CHECK_MSG(contains(glsl, "gl_GlobalInvocationID.x"), "invocation opcode must map to the global id");
    CHECK_MSG(contains(glsl, "fma("), "fma must lower to the GLSL fma intrinsic");
    CHECK_MSG(contains(glsl, " ? "), "select must lower to a ternary");
    CHECK_MSG(contains(glsl, " > "), "comparisons must be emitted with the real operator");
    CHECK_MSG(contains(glsl, "uint(") && contains(glsl, ".length()"),
              "loads and stores must carry bounds checks against the runtime array length");
    CHECK_MSG(glsl.size() > 200, "generated shader must not be a stub");
}

HCS_TEST(ir_glsl_lowering_is_deterministic)
{
    Module module = compileOrDie(kVecAdd);
    std::string first;
    std::string second;
    HCS_REQUIRE_OK(hcs::ir::lowerGlsl(module, &first));
    HCS_REQUIRE_OK(hcs::ir::lowerGlsl(module, &second));
    CHECK_EQ(first, second);
}

HCS_TEST(ir_glsl_lowering_rejects_barrier)
{
    const char* text = R"(hcsir 1
kernel ir_barrier
buffer 0 a : f32
local_size 64

entry:
  idx = invocation
  barrier
  ret
)";
    Module module = compileOrDie(text);
    std::string glsl;
    const hcsResult result = hcs::ir::lowerGlsl(module, &glsl);
    CHECK_EQ(result, HCS_E_UNSUPPORTED_FEATURE);
    CHECK_MSG(lastMessage().find("uniform control-flow") != std::string::npos, lastMessage());
    CHECK_MSG(glsl.empty(), "a rejected module must not produce partial output");
}

HCS_TEST(ir_glsl_lowering_rejects_uncompiled_module)
{
    Module module;
    std::string glsl;
    CHECK_EQ(hcs::ir::lowerGlsl(module, &glsl), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcs::ir::lowerGlsl(module, nullptr), HCS_E_INVALID_ARGUMENT);
}
