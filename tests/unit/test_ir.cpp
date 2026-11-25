#include "test_framework.h"

#include "ir/hcsir.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

using hcs::ir::Module;
using hcs::ir::RunArgs;

const char* kVecAdd = R"(hcsir 1
kernel ir_vecadd
buffer 0 a : f32
buffer 1 b : f32
buffer 2 c : f32
param alpha : f32
local_size 64

entry:
  idx = invocation
  va = load a, idx
  vb = load b, idx
  sum = fma alpha, vb, va
  store c, idx, sum
  ret
)";

/* Halve values that are not positive, keep positive values. */
const char* kBranch = R"(hcsir 1
kernel ir_branch
buffer 0 x : f32
buffer 1 y : f32
const zero : f32 = 0.0
const half : f32 = 0.5
local_size 32

entry:
  idx = invocation
  v = load x, idx
  positive = gt v, zero
  branch positive, keep, negate

keep:
  store y, idx, v
  ret

negate:
  scaled = mul v, half
  store y, idx, scaled
  ret
)";

hcsResult compileText(const char* text, Module* module)
{
    return hcs::ir::compile(text, std::strlen(text), module);
}

std::string lastMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    return info.message;
}

RunArgs makeArgs(std::vector<std::vector<float>*> buffers)
{
    RunArgs args;
    args.bufferCount = static_cast<uint32_t>(buffers.size());
    for (uint32_t i = 0; i < args.bufferCount; ++i) {
        args.buffers[i] = buffers[i]->data();
        args.sizes[i] = static_cast<uint64_t>(buffers[i]->size()) * sizeof(float);
    }
    return args;
}

} // namespace

HCS_TEST(ir_vecadd_matches_reference)
{
    Module module;
    HCS_REQUIRE_OK(compileText(kVecAdd, &module));
    CHECK_EQ(module.kernel, std::string("ir_vecadd"));
    CHECK_EQ(module.localSize, 64u);
    CHECK_EQ(module.buffers.size(), std::size_t{3});
    CHECK_EQ(module.params.size(), std::size_t{1});

    const uint32_t n = 1000; /* deliberately not a multiple of the local size */
    std::vector<float> a(n), b(n), c(n, 0.0f);
    for (uint32_t i = 0; i < n; ++i) {
        a[i] = static_cast<float>(i) * 0.001f - 0.25f;
        b[i] = static_cast<float>(i % 17) * 0.5f;
    }
    std::vector<std::vector<float>*> buffers{&a, &b, &c};
    RunArgs args = makeArgs(buffers);
    args.paramCount = 1;
    args.params[0] = 1.5;

    HCS_REQUIRE_OK(hcs::ir::run(module, args, n));

    for (uint32_t i = 0; i < n; ++i) {
        const double expected = static_cast<double>(a[i]) + 1.5 * static_cast<double>(b[i]);
        CHECK_NEAR(c[i], expected, 1e-5);
    }
}

HCS_TEST(ir_branch_and_select_execute)
{
    Module module;
    HCS_REQUIRE_OK(compileText(kBranch, &module));
    CHECK_EQ(module.blocks.size(), std::size_t{3});
    CHECK_EQ(module.constants.size(), std::size_t{2});

    const uint32_t n = 257;
    std::vector<float> x(n), y(n, 0.0f);
    for (uint32_t i = 0; i < n; ++i) {
        x[i] = static_cast<float>(static_cast<int>(i) - 128);
    }
    std::vector<std::vector<float>*> buffers{&x, &y};
    RunArgs args = makeArgs(buffers);

    HCS_REQUIRE_OK(hcs::ir::run(module, args, n));
    for (uint32_t i = 0; i < n; ++i) {
        const double expected = x[i] > 0.0f ? static_cast<double>(x[i])
                                            : 0.5 * static_cast<double>(x[i]);
        CHECK_NEAR(y[i], expected, 0.0);
    }
}

HCS_TEST(ir_rejects_unknown_version)
{
    Module module;
    const hcsResult result = compileText("hcsir 2\nkernel k\nlocal_size 1\nentry:\n  ret\n", &module);
    CHECK_EQ(result, HCS_E_UNSUPPORTED_API_VERSION);
    CHECK_MSG(lastMessage().find("HCSIR line 1") != std::string::npos,
              "diagnostic must carry the source line");
}

HCS_TEST(ir_rejects_unknown_opcode)
{
    Module module;
    const hcsResult result = compileText(
        "hcsir 1\nkernel k\nbuffer 0 a : f32\nlocal_size 1\nentry:\n"
        "  idx = invocation\n  v = hload.atomic a, idx, 0.0\n  ret\n",
        &module);
    CHECK_EQ(result, HCS_E_UNSUPPORTED_FEATURE);
    CHECK_MSG(lastMessage().find("unknown opcode") != std::string::npos, lastMessage());
}

HCS_TEST(ir_rejects_undefined_value)
{
    Module module;
    const hcsResult result = compileText(
        "hcsir 1\nkernel k\nbuffer 0 a : f32\nlocal_size 1\nentry:\n"
        "  idx = invocation\n  v = load a, missing\n  ret\n",
        &module);
    CHECK_EQ(result, HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("undefined value") != std::string::npos, lastMessage());
}

HCS_TEST(ir_rejects_type_mismatch)
{
    Module module;
    const hcsResult result = compileText(
        "hcsir 1\nkernel k\nbuffer 0 a : f32\nlocal_size 1\nentry:\n"
        "  idx = invocation\n  v = load a, idx\n  w = add v, idx\n  ret\n",
        &module);
    CHECK_EQ(result, HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("arithmetic requires f32") != std::string::npos, lastMessage());
}

HCS_TEST(ir_rejects_missing_terminator)
{
    Module module;
    const hcsResult result = compileText(
        "hcsir 1\nkernel k\nbuffer 0 a : f32\nlocal_size 1\nentry:\n"
        "  idx = invocation\n  v = load a, idx\n",
        &module);
    CHECK_EQ(result, HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("no terminator") != std::string::npos, lastMessage());
}

HCS_TEST(ir_rejects_unknown_branch_target)
{
    Module module;
    const hcsResult result = compileText(
        "hcsir 1\nkernel k\nbuffer 0 a : f32\nlocal_size 1\nentry:\n"
        "  idx = invocation\n  jump nowhere\n",
        &module);
    CHECK_EQ(result, HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("does not exist") != std::string::npos, lastMessage());
}

HCS_TEST(ir_rejects_oversized_source)
{
    std::string text = "hcsir 1\nkernel k\nlocal_size 1\nentry:\n";
    text.append(hcs::ir::kMaxSourceBytes + 1024, '#');
    text.append("\n  ret\n");
    Module module;
    const hcsResult result = hcs::ir::compile(text.data(), text.size(), &module);
    CHECK_EQ(result, HCS_E_SECURITY_POLICY);
}

HCS_TEST(ir_rejects_out_of_bounds_access)
{
    Module module;
    HCS_REQUIRE_OK(compileText(kVecAdd, &module));
    std::vector<float> a(8), b(8), c(8, 0.0f);
    std::vector<std::vector<float>*> buffers{&a, &b, &c};
    RunArgs args = makeArgs(buffers);
    /* 1024 invocations against 8-element buffers: load must fail, not read past. */
    const hcsResult result = hcs::ir::run(module, args, 1024);
    CHECK_EQ(result, HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("out of bounds") != std::string::npos, lastMessage());
}

HCS_TEST(ir_rejects_unbound_buffer)
{
    Module module;
    HCS_REQUIRE_OK(compileText(kVecAdd, &module));
    std::vector<float> a(8);
    RunArgs args;
    args.bufferCount = 1;
    args.buffers[0] = a.data();
    args.sizes[0] = 8 * sizeof(float);
    CHECK_EQ(hcs::ir::run(module, args, 1), HCS_E_INVALID_ARGUMENT);
}

HCS_TEST(ir_rejects_zero_and_huge_invocation_counts)
{
    Module module;
    HCS_REQUIRE_OK(compileText(kVecAdd, &module));
    std::vector<float> a(8), b(8), c(8, 0.0f);
    std::vector<std::vector<float>*> buffers{&a, &b, &c};
    RunArgs args = makeArgs(buffers);
    /* Zero invocations is a no-op, not an error. */
    CHECK_EQ(hcs::ir::run(module, args, 0), HCS_SUCCESS);
    /* Above the documented 4M bound the call is refused up front. */
    CHECK_EQ(hcs::ir::run(module, args, (1u << 22) + 1), HCS_E_SECURITY_POLICY);
}
