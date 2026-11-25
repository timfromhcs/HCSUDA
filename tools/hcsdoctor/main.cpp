/*
 * HCSDOCTOR — device and capability report for HCSUDA.
 * Exit codes: 0 = at least one device; 1 = runtime error; 2 = no device.
 */

#include "hcsuda/hcsuda.h"

#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace {

void printCapabilities(uint32_t caps)
{
    struct Entry {
        uint32_t bit;
        const char* name;
    };
    const Entry entries[] = {
        {HCS_CAP_BASE_COMPUTE, "base"},
        {HCS_CAP_MEMORY_BUDGET, "memory_budget"},
        {HCS_CAP_ASYNC, "async"},
        {HCS_CAP_SUBGROUP, "subgroup"},
        {HCS_CAP_FP16, "fp16"},
        {HCS_CAP_INT8, "int8"},
        {HCS_CAP_MATRIX, "matrix"},
        {HCS_CAP_AVX2, "avx2"},
        {HCS_CAP_TIMELINE_SEMAPHORE, "timeline_semaphore"},
    };
    bool first = true;
    for (const auto& entry : entries) {
        if ((caps & entry.bit) != 0) {
            std::printf("%s%s", first ? "" : " ", entry.name);
            first = false;
        }
    }
    if (first) {
        std::printf("none");
    }
}

std::string escapeJson(const char* text)
{
    std::string out;
    for (const char* p = text; *p; ++p) {
        const unsigned char c = static_cast<unsigned char>(*p);
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(static_cast<char>(c));
        } else if (c < 0x20) {
            out.push_back(' ');
        } else {
            out.push_back(static_cast<char>(c));
        }
    }
    return out;
}

double mib(uint64_t bytes) { return static_cast<double>(bytes) / (1024.0 * 1024.0); }

uint32_t vkMajor(uint32_t v) { return (v >> 22) & 0x7Fu; }
uint32_t vkMinor(uint32_t v) { return (v >> 12) & 0x3FFu; }
uint32_t vkPatch(uint32_t v) { return v & 0xFFFu; }

} // namespace

int main(int argc, char** argv)
{
    bool json = false;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--json") == 0) {
            json = true;
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: hcsdoctor [--json]\n");
            return 0;
        }
    }

    const hcsResult init = hcsInit(nullptr);
    if (init != HCS_SUCCESS) {
        std::fprintf(stderr, "hcsInit failed: %s\n", hcsResultString(init));
        return 1;
    }

    uint32_t count = 0;
    hcsResult result = hcsEnumerateDevices(nullptr, &count);
    if (result != HCS_SUCCESS) {
        std::fprintf(stderr, "device enumeration failed: %s\n", hcsResultString(result));
        hcsShutdown();
        return 1;
    }
    std::vector<hcsDeviceInfo> infos(count);
    result = hcsEnumerateDevices(infos.data(), &count);
    if (result != HCS_SUCCESS) {
        std::fprintf(stderr, "device enumeration failed: %s\n", hcsResultString(result));
        hcsShutdown();
        return 1;
    }

    if (json) {
        std::printf("{\"product\":\"%s\",\"version\":\"%s\",\"api_version\":%u,\"devices\":[",
                    HCS_PRODUCT_NAME, hcsGetVersionString(), hcsGetApiVersion());
        for (uint32_t i = 0; i < count; ++i) {
            hcsDevice device = nullptr;
            if (hcsGetDevice(i, &device) != HCS_SUCCESS) {
                continue;
            }
            hcsDeviceCaps caps{};
            caps.struct_size = sizeof(caps);
            hcsGetDeviceCaps(device, &caps);
            hcsMemoryBudget budget{};
            budget.struct_size = sizeof(budget);
            hcsGetMemoryBudget(device, &budget);
            char apiText[32] = {};
            if (infos[i].backend == HCS_BACKEND_CPU) {
                std::snprintf(apiText, sizeof(apiText), "hcs-%u", infos[i].api_version);
            } else {
                std::snprintf(apiText, sizeof(apiText), "%u.%u.%u",
                              vkMajor(infos[i].api_version),
                              vkMinor(infos[i].api_version),
                              vkPatch(infos[i].api_version));
            }
            std::printf("%s{\"index\":%u,\"name\":\"%s\",\"backend\":\"%s\",\"kind\":\"%s\","
                        "\"vendor\":\"%s\",\"vendor_id\":%u,\"device_id\":%u,"
                        "\"api_version\":\"%s\",\"driver_version\":%u,"
                        "\"capabilities\":%u,\"max_workgroup_invocations\":%u,"
                        "\"subgroup_size\":%u,\"heap_bytes\":%llu,\"budget_bytes\":%llu,"
                        "\"usage_bytes\":%llu}",
                        i == 0 ? "" : ",", i, escapeJson(infos[i].name).c_str(),
                        hcsBackendName(infos[i].backend), hcsDeviceKindName(infos[i].kind),
                        escapeJson(infos[i].vendor).c_str(), infos[i].vendor_id,
                        infos[i].device_number, apiText,
                        infos[i].driver_version, caps.capabilities,
                        caps.max_workgroup_invocations, caps.subgroup_size,
                        static_cast<unsigned long long>(budget.heap_bytes),
                        static_cast<unsigned long long>(budget.budget_bytes),
                        static_cast<unsigned long long>(budget.usage_bytes));
        }
        std::printf("]}\n");
        hcsShutdown();
        return count > 0 ? 0 : 2;
    }

    std::printf("%s %s (%s)\n", HCS_PRODUCT_NAME, hcsGetVersionString(),
                HCS_PRODUCT_FULL_NAME);
    std::printf("C ABI version %u | %s\n\n", hcsGetApiVersion(), HCS_PRODUCT_TAGLINE);
    std::printf("Devices found: %u\n\n", count);

    for (uint32_t i = 0; i < count; ++i) {
        hcsDevice device = nullptr;
        if (hcsGetDevice(i, &device) != HCS_SUCCESS) {
            continue;
        }
        hcsDeviceCaps caps{};
        caps.struct_size = sizeof(caps);
        hcsGetDeviceCaps(device, &caps);
        hcsMemoryBudget budget{};
        budget.struct_size = sizeof(budget);
        hcsGetMemoryBudget(device, &budget);

        const hcsDeviceInfo& info = infos[i];
        std::printf("[%u] %s\n", i, info.name);
        std::printf("    backend      : %s\n", hcsBackendName(info.backend));
        std::printf("    kind         : %s%s\n", hcsDeviceKindName(info.kind),
                    (info.flags & HCS_DEVICE_FLAG_INTEGRATED) ? " (unified memory)" : "");
        std::printf("    vendor       : %s (0x%04X), device 0x%04X\n", info.vendor,
                    info.vendor_id, info.device_number);
        std::printf("    api/driver   : ");
        if (info.backend == HCS_BACKEND_CPU) {
            std::printf("HCS API %u / host runtime\n", info.api_version);
        } else {
            std::printf("%u.%u.%u / 0x%08X\n", vkMajor(info.api_version),
                        vkMinor(info.api_version), vkPatch(info.api_version),
                        info.driver_version);
        }
        std::printf("    driver info  : %s\n", info.driver_info);
        std::printf("    capabilities : ");
        printCapabilities(caps.capabilities);
        std::printf("\n");
        std::printf("    workgroup    : %u invocations, max %ux%ux%u, shared %llu KiB\n",
                    caps.max_workgroup_invocations, caps.max_workgroup_size[0],
                    caps.max_workgroup_size[1], caps.max_workgroup_size[2],
                    static_cast<unsigned long long>(caps.shared_memory_bytes / 1024));
        std::printf("    subgroup     : %u\n", caps.subgroup_size);
        std::printf("    memory       : heap %.1f MiB, budget %.1f MiB, used %.1f MiB\n",
                    mib(budget.heap_bytes), mib(budget.budget_bytes),
                    mib(budget.usage_bytes));
        std::printf("\n");
    }

    if (count == 0) {
        std::printf("No devices. The CPU backend should always be present; this is a bug.\n");
    }

    hcsShutdown();
    return count > 0 ? 0 : 2;
}
