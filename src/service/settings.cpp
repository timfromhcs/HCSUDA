#include "service/settings.h"

#include "core/internal.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>

namespace hcs::service {
namespace {

std::string escape(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n' || c == '\r') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string stringField(const std::string& text, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t start = text.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    size_t i = start + needle.size();
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    if (i >= text.size() || text[i] != '"') {
        return {};
    }
    ++i;
    std::string value;
    for (; i < text.size(); ++i) {
        if (text[i] == '\\' && i + 1 < text.size()) {
            value.push_back(text[++i]);
        } else if (text[i] == '"') {
            break;
        } else {
            value.push_back(text[i]);
        }
    }
    return value;
}

uint32_t numberField(const std::string& text, const std::string& key, uint32_t fallback)
{
    const size_t start = text.find("\"" + key + "\":");
    if (start == std::string::npos) {
        return fallback;
    }
    size_t i = start + key.size() + 3;
    while (i < text.size() && (std::isspace(static_cast<unsigned char>(text[i])) ||
                               text[i] == '"')) {
        ++i;
    }
    if (i >= text.size() || !std::isdigit(static_cast<unsigned char>(text[i]))) {
        return fallback;
    }
    return static_cast<uint32_t>(std::strtoul(text.c_str() + i, nullptr, 10));
}

bool boolField(const std::string& text, const std::string& key, bool fallback)
{
    const size_t start = text.find("\"" + key + "\":true");
    if (start != std::string::npos) {
        return true;
    }
    if (text.find("\"" + key + "\":false") != std::string::npos) {
        return false;
    }
    return fallback;
}

bool validDirectory(const std::string& value)
{
    if (value.empty()) {
        return true;
    }
    if (value.size() > 4096 || value.find("..") != std::string::npos) {
        return false;
    }
    const std::filesystem::path path(value);
    return path.is_absolute();
}

} // namespace

SettingsStore::SettingsStore(std::filesystem::path configDir)
{
    if (configDir.empty()) {
        if (const char* local = std::getenv("LOCALAPPDATA")) {
            configDir_ = std::filesystem::path(local) / "HCSUDA";
        } else {
            configDir_ = std::filesystem::temp_directory_path() / "HCSUDA";
        }
    } else {
        configDir_ = std::move(configDir);
    }
    path_ = configDir_ / "settings.json";
}

hcsResult SettingsStore::load()
{
    std::ifstream file(path_, std::ios::binary);
    if (!file) {
        settings_ = Settings{};
        return HCS_SUCCESS;
    }
    std::string text((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());
    if (text.size() > 64u << 10) {
        settings_ = Settings{};
        return fail(HCS_E_SECURITY_POLICY, "settings.load", "settings file exceeds 64 KiB");
    }
    Settings parsed;
    parsed.backendPolicy = numberField(text, "backend_policy", 0);
    parsed.memoryCapMiB = numberField(text, "memory_cap_mib", 0);
    parsed.cpuThreads = numberField(text, "cpu_threads", 0);
    parsed.logVerbosity = numberField(text, "log_verbosity", 1);
    parsed.apiPort = static_cast<uint16_t>(numberField(text, "api_port", 8710));
    parsed.modelDir = stringField(text, "model_dir");
    parsed.stagingDir = stringField(text, "staging_dir");
    parsed.shaderCacheDir = stringField(text, "shader_cache_dir");
    parsed.prefetchPolicy = stringField(text, "prefetch_policy");
    parsed.theme = stringField(text, "theme");
    parsed.startWithWindows = boolField(text, "start_with_windows", false);
    parsed.updateCheck = boolField(text, "update_check", false);
    if (parsed.backendPolicy > 2 || parsed.memoryCapMiB > 64u << 10 || parsed.cpuThreads > 256 ||
        parsed.logVerbosity > 3 || parsed.apiPort < 1024 ||
        !validDirectory(parsed.modelDir) || !validDirectory(parsed.stagingDir) ||
        !validDirectory(parsed.shaderCacheDir) ||
        (parsed.prefetchPolicy != "off" && parsed.prefetchPolicy != "conservative" &&
         parsed.prefetchPolicy != "adaptive") ||
        (parsed.theme != "dark" && parsed.theme != "light")) {
        settings_ = Settings{};
        return fail(HCS_E_INVALID_ARGUMENT, "settings.load",
                    "stored settings failed validation; defaults restored");
    }
    settings_ = parsed;
    return HCS_SUCCESS;
}

hcsResult SettingsStore::save() const
{
    std::error_code error;
    std::filesystem::create_directories(configDir_, error);
    if (error) {
        return fail(HCS_E_IO, "settings.save", "cannot create settings directory");
    }
    const std::filesystem::path temp = path_.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return fail(HCS_E_IO, "settings.save", "cannot write settings temporary file");
        }
        file << toJson() << '\n';
    }
    std::filesystem::remove(path_, error);
    std::filesystem::rename(temp, path_, error);
    if (error) {
        return fail(HCS_E_IO, "settings.save", "cannot finalize settings: " + error.message());
    }
    return HCS_SUCCESS;
}

hcsResult SettingsStore::reset()
{
    settings_ = Settings{};
    return save();
}

hcsResult SettingsStore::updateFromJson(const std::string& json)
{
    if (json.empty() || json.size() > 16u << 10) {
        return fail(HCS_E_INVALID_ARGUMENT, "settings.update", "settings body is invalid");
    }
    Settings candidate = settings_;
    const std::string backend = stringField(json, "backend_policy");
    if (backend == "auto") candidate.backendPolicy = 0;
    else if (backend == "vulkan") candidate.backendPolicy = 1;
    else if (backend == "cpu") candidate.backendPolicy = 2;
    else if (!backend.empty()) return fail(HCS_E_INVALID_ARGUMENT, "settings.update", "invalid backend policy");
    else candidate.backendPolicy = numberField(json, "backend_policy", candidate.backendPolicy);

    candidate.memoryCapMiB = numberField(json, "memory_cap_mib", candidate.memoryCapMiB);
    candidate.cpuThreads = numberField(json, "cpu_threads", candidate.cpuThreads);
    candidate.logVerbosity = numberField(json, "log_verbosity", candidate.logVerbosity);
    const uint32_t port = numberField(json, "api_port", candidate.apiPort);
    candidate.apiPort = static_cast<uint16_t>(port);
    const std::string modelDir = stringField(json, "model_dir");
    const std::string stagingDir = stringField(json, "staging_dir");
    const std::string cacheDir = stringField(json, "shader_cache_dir");
    const std::string prefetch = stringField(json, "prefetch_policy");
    const std::string theme = stringField(json, "theme");
    if (!modelDir.empty()) candidate.modelDir = modelDir;
    if (!stagingDir.empty()) candidate.stagingDir = stagingDir;
    if (!cacheDir.empty()) candidate.shaderCacheDir = cacheDir;
    if (!prefetch.empty()) candidate.prefetchPolicy = prefetch;
    if (!theme.empty()) candidate.theme = theme;
    candidate.startWithWindows = boolField(json, "start_with_windows", candidate.startWithWindows);
    candidate.updateCheck = boolField(json, "update_check", candidate.updateCheck);

    if (candidate.backendPolicy > 2 || candidate.memoryCapMiB > (64u << 10) ||
        candidate.cpuThreads > 256 || candidate.logVerbosity > 3 || candidate.apiPort < 1024 ||
        !validDirectory(candidate.modelDir) || !validDirectory(candidate.stagingDir) ||
        !validDirectory(candidate.shaderCacheDir) ||
        (candidate.prefetchPolicy != "off" && candidate.prefetchPolicy != "conservative" &&
         candidate.prefetchPolicy != "adaptive") ||
        (candidate.theme != "dark" && candidate.theme != "light")) {
        return fail(HCS_E_INVALID_ARGUMENT, "settings.update",
                    "one or more settings failed validation");
    }
    settings_ = candidate;
    return save();
}

std::string SettingsStore::toJson() const
{
    std::string json = "{";
    json += "\"backend_policy\":" + std::to_string(settings_.backendPolicy) + ",";
    json += "\"memory_cap_mib\":" + std::to_string(settings_.memoryCapMiB) + ",";
    json += "\"cpu_threads\":" + std::to_string(settings_.cpuThreads) + ",";
    json += "\"api_port\":" + std::to_string(settings_.apiPort) + ",";
    json += "\"log_verbosity\":" + std::to_string(settings_.logVerbosity) + ",";
    json += "\"model_dir\":\"" + escape(settings_.modelDir) + "\",";
    json += "\"staging_dir\":\"" + escape(settings_.stagingDir) + "\",";
    json += "\"shader_cache_dir\":\"" + escape(settings_.shaderCacheDir) + "\",";
    json += "\"prefetch_policy\":\"" + escape(settings_.prefetchPolicy) + "\",";
    json += "\"theme\":\"" + escape(settings_.theme) + "\",";
    json += "\"start_with_windows\":" +
            std::string(settings_.startWithWindows ? "true" : "false") + ",";
    json += "\"update_check\":" + std::string(settings_.updateCheck ? "true" : "false");
    return json + "}";
}

} // namespace hcs::service
