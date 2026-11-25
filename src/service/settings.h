#pragma once

#include "hcsuda/hcsuda.h"

#include <cstdint>
#include <filesystem>
#include <string>

namespace hcs::service {

struct Settings {
    uint32_t backendPolicy = 0;    /* 0 auto, 1 Vulkan, 2 CPU */
    uint32_t memoryCapMiB = 0;     /* 0 = runtime default */
    uint32_t cpuThreads = 0;       /* 0 = auto */
    std::string modelDir;
    std::string stagingDir;
    std::string shaderCacheDir;
    std::string prefetchPolicy = "off"; /* off | conservative | adaptive */
    uint32_t logVerbosity = 1;     /* 0..3 */
    uint16_t apiPort = 8710;
    std::string theme = "dark";    /* dark | light */
    bool startWithWindows = false;
    bool updateCheck = false;
};

class SettingsStore {
public:
    explicit SettingsStore(std::filesystem::path configDir);

    hcsResult load();
    hcsResult save() const;
    hcsResult reset();
    hcsResult updateFromJson(const std::string& json);

    const Settings& get() const { return settings_; }
    std::string toJson() const;

private:
    std::filesystem::path configDir_;
    std::filesystem::path path_;
    Settings settings_;
};

} // namespace hcs::service
