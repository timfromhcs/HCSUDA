#include "hcsuda/hcsuda.hpp"
#include "test_helpers.h"

#include <cstring>
#include <string>
#include <vector>

using namespace testutil;

HCS_TEST(version_identity)
{
    CHECK_EQ(std::string(hcsGetVersionString()), std::string("0.1.0"));
    CHECK_EQ(hcsGetApiVersion(), 1u);
    CHECK(std::strcmp(hcsResultString(HCS_SUCCESS), "HCS_SUCCESS") == 0);
    CHECK(std::strcmp(hcsResultString(HCS_E_KERNEL_NOT_FOUND), "HCS_E_KERNEL_NOT_FOUND") == 0);
    CHECK(std::strcmp(hcsResultString(static_cast<hcsResult>(999)), "HCS_E_UNKNOWN") == 0);
}

HCS_TEST(init_requires_once)
{
    HCS_REQUIRE_OK(hcsInit(nullptr));
    CHECK_EQ(hcsInit(nullptr), HCS_E_ALREADY_INITIALIZED);
    HCS_REQUIRE_OK(hcsShutdown());
    /* A second shutdown is a no-op, not an error. */
    HCS_REQUIRE_OK(hcsShutdown());
}

HCS_TEST(enumerate_devices_reports_real_hardware)
{
    HCS_REQUIRE_OK(hcsInit(nullptr));
    uint32_t count = 0;
    HCS_REQUIRE_OK(hcsEnumerateDevices(nullptr, &count));
    CHECK_MSG(count >= 1, "at least the CPU backend must always be present");
    CHECK_MSG(count <= HCS_MAX_DEVICES, "device count exceeds the documented maximum");

    std::vector<hcsDeviceInfo> infos(count);
    HCS_REQUIRE_OK(hcsEnumerateDevices(infos.data(), &count));
    bool sawCpu = false;
    for (const auto& info : infos) {
        (void)info;
    }
    for (uint32_t i = 0; i < count; ++i) {
        const hcsDeviceInfo& info = infos[i];
        CHECK_MSG(info.name[0] != '\0', "device name must not be empty");
        CHECK_MSG(info.backend == HCS_BACKEND_CPU || info.backend == HCS_BACKEND_VULKAN,
                  "unexpected backend kind");
        CHECK_MSG(info.device_id != 0, "device id must be stable and non-zero");
        if (info.backend == HCS_BACKEND_CPU) {
            sawCpu = true;
        }
    }
    CHECK_MSG(sawCpu, "the CPU backend must always be enumerable");
    HCS_REQUIRE_OK(hcsShutdown());
}

HCS_TEST(invalid_arguments_are_rejected)
{
    HCS_REQUIRE_OK(hcsInit(nullptr));
    CHECK_EQ(hcsEnumerateDevices(nullptr, nullptr), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsCreateContext(nullptr, nullptr, nullptr), HCS_E_INVALID_ARGUMENT);

    hcsDevice device = nullptr;
    HCS_REQUIRE_OK(hcsGetDevice(0, &device));
    CHECK_EQ(hcsGetDeviceCaps(nullptr, nullptr), HCS_E_INVALID_ARGUMENT);

    hcsDeviceCaps caps{};
    caps.struct_size = sizeof(caps);
    HCS_REQUIRE_OK(hcsGetDeviceCaps(device, &caps));
    CHECK_MSG((caps.capabilities & HCS_CAP_BASE_COMPUTE) != 0,
              "every device must support base compute");
    HCS_REQUIRE_OK(hcsShutdown());
}

HCS_TEST(kernel_lookup_is_strict)
{
    HCS_REQUIRE_OK(hcsInit(nullptr));
    hcsDevice device = nullptr;
    HCS_REQUIRE_OK(findDevice(HCS_BACKEND_CPU, &device));
    hcsContext context = nullptr;
    hcsContextDesc cdesc{};
    cdesc.struct_size = sizeof(cdesc);
    HCS_REQUIRE_OK(hcsCreateContext(device, &cdesc, &context));

    hcsKernel kernel = nullptr;
    CHECK_EQ(hcsKernelCreate(context, "does_not_exist", &kernel), HCS_E_KERNEL_NOT_FOUND);
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    CHECK_EQ(hcsGetLastError(&info), HCS_E_KERNEL_NOT_FOUND);
    CHECK(std::strstr(info.message, "does_not_exist") != nullptr);
    CHECK(info.correlation_id[0] != '\0');

    HCS_REQUIRE_OK(hcsKernelCreate(context, "vecadd_f32", &kernel));
    CHECK(kernel != nullptr);
    hcsKernelDestroy(kernel);
    hcsDestroyContext(context);
    HCS_REQUIRE_OK(hcsShutdown());
}

HCS_TEST(buffer_bounds_are_checked)
{
    HCS_REQUIRE_OK(hcsInit(nullptr));
    hcsDevice device = nullptr;
    HCS_REQUIRE_OK(findDevice(HCS_BACKEND_CPU, &device));
    hcsContext context = nullptr;
    hcsContextDesc cdesc{};
    cdesc.struct_size = sizeof(cdesc);
    HCS_REQUIRE_OK(hcsCreateContext(device, &cdesc, &context));

    hcsBufferDesc bdesc{};
    bdesc.struct_size = sizeof(bdesc);
    bdesc.size = 64;
    hcsBuffer buffer = nullptr;
    HCS_REQUIRE_OK(hcsBufferCreate(context, &bdesc, &buffer));
    CHECK_EQ(hcsBufferSize(buffer), 64u);

    unsigned char data[64] = {};
    CHECK_EQ(hcsBufferUpload(buffer, 40, data, 32), HCS_E_INVALID_ARGUMENT);
    HCS_REQUIRE_OK(hcsBufferUpload(buffer, 32, data, 32));
    CHECK_EQ(hcsBufferDownload(buffer, 0, data, 65), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(hcsBufferClear(buffer, 60, 8), HCS_E_INVALID_ARGUMENT);

    hcsBufferDestroy(buffer);
    hcsDestroyContext(context);
    HCS_REQUIRE_OK(hcsShutdown());
}

HCS_TEST(dispatch_requires_matching_context)
{
    HCS_REQUIRE_OK(hcsInit(nullptr));
    hcsDevice device = nullptr;
    HCS_REQUIRE_OK(findDevice(HCS_BACKEND_CPU, &device));
    hcsContext context = nullptr;
    hcsContextDesc cdesc{};
    cdesc.struct_size = sizeof(cdesc);
    HCS_REQUIRE_OK(hcsCreateContext(device, &cdesc, &context));

    hcsKernel kernel = nullptr;
    HCS_REQUIRE_OK(hcsKernelCreate(context, "vecadd_f32", &kernel));
    hcsDispatchDesc desc{};
    desc.struct_size = sizeof(desc);
    desc.group_count_x = 1;
    CHECK_EQ(hcsDispatch(kernel, &desc, nullptr), HCS_E_INVALID_ARGUMENT);

    hcsDispatchDesc bad = desc;
    bad.struct_size = 12;
    CHECK_EQ(hcsDispatch(kernel, &bad, nullptr), HCS_E_INVALID_ARGUMENT);

    hcsDispatchDesc noGroups = desc;
    noGroups.group_count_x = 0;
    CHECK_EQ(hcsDispatch(kernel, &noGroups, nullptr), HCS_E_INVALID_ARGUMENT);

    hcsKernelDestroy(kernel);
    hcsDestroyContext(context);
    HCS_REQUIRE_OK(hcsShutdown());
}
