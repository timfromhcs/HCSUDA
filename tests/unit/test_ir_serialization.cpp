#include "test_framework.h"

#include "ir/hcsir.h"

#include <cstring>
#include <string>
#include <vector>

namespace {

const char* kModule = R"(hcsir 1
kernel ser_vecadd
buffer 0 a : f32
buffer 1 b : f32
buffer 2 c : f32
param alpha : f32
const zero : f32 = 0.0
local_size 64

entry:
  i = invocation
  va = load a, i
  vb = load b, i
  sum = fma alpha, vb, va
  store c, i, sum
  ret
)";

std::string lastMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    return info.message;
}

hcsResult runModule(const hcs::ir::Module& module, std::vector<float>* c)
{
    const uint32_t n = static_cast<uint32_t>(c->size());
    std::vector<float> a(n, 0.25f), b(n, 0.5f);
    hcs::ir::RunArgs args;
    args.bufferCount = 3;
    args.buffers[0] = a.data();
    args.sizes[0] = n * sizeof(float);
    args.buffers[1] = b.data();
    args.sizes[1] = n * sizeof(float);
    args.buffers[2] = c->data();
    args.sizes[2] = n * sizeof(float);
    args.paramCount = 1;
    args.params[0] = 1.5;
    return hcs::ir::run(module, args, n);
}

} // namespace

HCS_TEST(ir_serialization_roundtrip_preserves_semantics)
{
    hcs::ir::Module original;
    HCS_REQUIRE_OK(hcs::ir::compile(kModule, std::strlen(kModule), &original));
    std::string bytes;
    HCS_REQUIRE_OK(hcs::ir::serialize(original, &bytes));
    CHECK(bytes.size() > 64);

    hcs::ir::Module restored;
    HCS_REQUIRE_OK(hcs::ir::deserialize(bytes, &restored));
    CHECK_EQ(restored.kernel, original.kernel);
    CHECK_EQ(restored.localSize, original.localSize);
    CHECK_EQ(restored.instructionCount, original.instructionCount);
    CHECK_EQ(restored.buffers.size(), original.buffers.size());
    CHECK_EQ(restored.params.size(), original.params.size());
    CHECK_EQ(restored.constants.size(), original.constants.size());
    CHECK_EQ(restored.valueTypes.size(), original.valueTypes.size());
    CHECK_EQ(restored.blocks.size(), original.blocks.size());

    std::string again;
    HCS_REQUIRE_OK(hcs::ir::serialize(restored, &again));
    CHECK_EQ(again, bytes); /* canonical byte-for-byte round-trip */

    std::vector<float> first(1000, 0.0f), second(1000, 0.0f);
    HCS_REQUIRE_OK(runModule(original, &first));
    HCS_REQUIRE_OK(runModule(restored, &second));
    CHECK_EQ(first, second);
}

HCS_TEST(ir_serialization_rejects_corrupt_modules)
{
    hcs::ir::Module module;
    HCS_REQUIRE_OK(hcs::ir::compile(kModule, std::strlen(kModule), &module));
    std::string bytes;
    HCS_REQUIRE_OK(hcs::ir::serialize(module, &bytes));

    hcs::ir::Module restored;
    std::string bad = bytes;
    bad[0] = 'X';
    CHECK_EQ(hcs::ir::deserialize(bad, &restored), HCS_E_INVALID_ARGUMENT);
    CHECK_MSG(lastMessage().find("magic") != std::string::npos, lastMessage());

    bad = bytes.substr(0, bytes.size() - 1);
    CHECK_EQ(hcs::ir::deserialize(bad, &restored), HCS_E_INVALID_ARGUMENT);
    bad = bytes + "x";
    CHECK_EQ(hcs::ir::deserialize(bad, &restored), HCS_E_INVALID_ARGUMENT);

    bad = bytes;
    bad[11] = 9; /* magic is bytes 0..7, version is bytes 8..11 */
    CHECK_EQ(hcs::ir::deserialize(bad, &restored), HCS_E_UNSUPPORTED_API_VERSION);

    CHECK_EQ(hcs::ir::deserialize("", &restored), HCS_E_SECURITY_POLICY);
    CHECK_EQ(hcs::ir::deserialize(bytes, nullptr), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcs::ir::serialize(module, nullptr), HCS_E_INVALID_ARGUMENT);
}

HCS_TEST(ir_deserialize_fuzz_is_bounded)
{
    uint32_t seed = 0x5EED1234u;
    hcs::ir::Module module;
    for (uint32_t iteration = 0; iteration < 200; ++iteration) {
        std::string bytes;
        const size_t length = (seed = seed * 1664525u + 1013904223u) % 512;
        for (size_t i = 0; i < length; ++i) {
            seed = seed * 1664525u + 1013904223u;
            bytes.push_back(static_cast<char>(seed & 0xFF));
        }
        (void)hcs::ir::deserialize(bytes, &module);
    }
}
