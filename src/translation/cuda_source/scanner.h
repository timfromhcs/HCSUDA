#pragma once

#include "hcsuda/hcsuda.h"

#include <cstdint>
#include <string>
#include <vector>

namespace hcs::cudascan {

constexpr uint32_t kMaxSourceBytes = 4u << 20;
constexpr uint32_t kMaxFindings = 4096;
constexpr uint32_t kMaxLineBytes = 16u << 10;

/* V1 scanner only. No construct is claimed as translated: that begins with a conformance
 * test suite in a later slice, at which point a `Translated` category may be added. */
enum class Classification { HostUnchanged, ManualPort, Unsupported };

struct Finding {
    uint32_t line = 0;
    uint32_t column = 0;
    std::string construct;
    std::string detail;
    Classification classification = Classification::ManualPort;
};

struct Report {
    uint64_t bytes = 0;
    uint32_t lines = 0;
    uint32_t manualCount = 0;
    uint32_t unsupportedCount = 0;
    std::vector<Finding> findings;
};

hcsResult scan(const char* text, size_t size, Report* out);
const char* classificationName(Classification value);
std::string toJson(const Report& report);

} // namespace hcs::cudascan
