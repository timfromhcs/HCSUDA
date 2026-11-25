#include "test_framework.h"

#include "service/settings.h"

#include <filesystem>
#include <fstream>
#include <string>

namespace fs = std::filesystem;

namespace {

fs::path testDir(const char* name)
{
    return fs::temp_directory_path() / "hcsuda-settings-tests" / name;
}

} // namespace

HCS_TEST(settings_defaults_are_safe)
{
    const fs::path dir = testDir("defaults");
    fs::remove_all(dir);
    hcs::service::SettingsStore store(dir);
    HCS_REQUIRE_OK(store.load());
    CHECK_EQ(store.get().backendPolicy, 0u);
    CHECK_EQ(store.get().memoryCapMiB, 0u);
    CHECK_EQ(store.get().cpuThreads, 0u);
    CHECK_EQ(store.get().apiPort, 8710u);
    CHECK(store.get().modelDir.empty());
    CHECK_EQ(store.get().prefetchPolicy, std::string("off"));
    CHECK_EQ(store.get().theme, std::string("dark"));
    CHECK(!store.get().startWithWindows);
    CHECK(!store.get().updateCheck);
}

HCS_TEST(settings_persist_and_reset)
{
    const fs::path dir = testDir("persist");
    fs::remove_all(dir);
    hcs::service::SettingsStore store(dir);
    HCS_REQUIRE_OK(store.load());
    HCS_REQUIRE_OK(store.updateFromJson(
        "{\"backend_policy\":\"cpu\",\"memory_cap_mib\":4096,\"cpu_threads\":4,"
        "\"api_port\":18712,\"theme\":\"light\",\"start_with_windows\":false,"
        "\"update_check\":true,\"prefetch_policy\":\"conservative\"}"));
    CHECK_EQ(store.get().backendPolicy, 2u);
    CHECK_EQ(store.get().memoryCapMiB, 4096u);
    CHECK_EQ(store.get().prefetchPolicy, std::string("conservative"));
    CHECK(store.get().updateCheck);

    hcs::service::SettingsStore reopened(dir);
    HCS_REQUIRE_OK(reopened.load());
    CHECK_EQ(reopened.get().apiPort, 18712u);
    CHECK_EQ(reopened.get().theme, std::string("light"));
    HCS_REQUIRE_OK(reopened.reset());
    CHECK_EQ(reopened.get().apiPort, 8710u);
    CHECK_EQ(reopened.get().backendPolicy, 0u);
}

HCS_TEST(settings_reject_invalid_values)
{
    const fs::path dir = testDir("invalid");
    fs::remove_all(dir);
    hcs::service::SettingsStore store(dir);
    HCS_REQUIRE_OK(store.load());
    CHECK_EQ(store.updateFromJson("{\"backend_policy\":\"cuda\"}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.updateFromJson("{\"api_port\":80}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.updateFromJson("{\"cpu_threads\":4096}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.updateFromJson("{\"theme\":\"neon\"}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.updateFromJson("{\"prefetch_policy\":\"always\"}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.updateFromJson("{\"model_dir\":\"../escape\"}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.updateFromJson("{\"model_dir\":\"relative/path\"}"), HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.get().apiPort, 8710u); /* failed updates leave state unchanged */
}

HCS_TEST(settings_recover_from_corrupt_file)
{
    const fs::path dir = testDir("corrupt");
    fs::remove_all(dir);
    fs::create_directories(dir);
    std::ofstream file(dir / "settings.json", std::ios::binary | std::ios::trunc);
    file << "{\"backend_policy\":99,\"api_port\":1}";
    file.close();
    hcs::service::SettingsStore store(dir);
    const hcsResult result = store.load();
    CHECK_EQ(result, HCS_E_INVALID_ARGUMENT);
    CHECK_EQ(store.get().apiPort, 8710u);
}
