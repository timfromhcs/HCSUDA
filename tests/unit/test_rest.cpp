#include "test_framework.h"
#include "test_helpers.h"

#include "api/rest/rest_server.h"

#include <filesystem>
#include <string>

namespace {

hcs::rest::RestConfig configWithToken(const std::string& token)
{
    hcs::rest::RestConfig config;
    config.token = token;
    return config;
}

std::string authHeader(const std::string& token)
{
    return "Authorization: Bearer " + token + "\r\n";
}

bool statusIs(const std::string& response, int code)
{
    return response.rfind("HTTP/1.1 " + std::to_string(code) + " ", 0) == 0;
}

} // namespace

HCS_TEST(rest_health_is_public_and_local_only)
{
    const auto config = configWithToken("secret");
    const std::string answer =
        hcs::rest::handleRequest("GET", "/api/v1/health", "", "", config);
    CHECK(statusIs(answer, 200));
    CHECK_MSG(answer.find("\"local_only\":true") != std::string::npos, answer);
    CHECK_MSG(answer.find("X-Content-Type-Options: nosniff") != std::string::npos, answer);
}

HCS_TEST(rest_requires_random_bearer_token)
{
    const std::string token = hcs::rest::generateToken();
    CHECK_EQ(token.size(), std::size_t{64});
    CHECK_MSG(token != hcs::rest::generateToken(), "tokens must be unique per process");
    CHECK(hcs::rest::constantTimeEquals(token, token));
    CHECK(!hcs::rest::constantTimeEquals(token, token.substr(1)));
    CHECK(!hcs::rest::constantTimeEquals(token, "0" + token.substr(1)));

    const auto config = configWithToken(token);
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/version", "", "", config), 401));
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/version",
                                            authHeader(token.substr(0, 63)) + "X", "", config),
                   401));
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/version", authHeader(token), "",
                                            config),
                   200));
}

HCS_TEST(rest_rejects_cross_origin_oversized_and_traversal)
{
    const auto config = configWithToken("token");
    const std::string tokenHeader = authHeader("token");
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/version",
                                            tokenHeader + "Origin: http://evil.example\r\n", "",
                                            config),
                   403));
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/version",
                                            tokenHeader + "Origin: http://localhost:3000\r\n", "",
                                            config),
                   200));
    CHECK(statusIs(hcs::rest::handleRequest("POST", "/api/v1/version", tokenHeader,
                                            std::string(hcs::rest::kMaxRequestBytes + 1, 'x'),
                                            config),
                   413));
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/../secret", tokenHeader, "",
                                            config),
                   400));
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/version\\evil", tokenHeader, "",
                                            config),
                   400));
    CHECK(statusIs(hcs::rest::handleRequest("GET", "/api/v1/nope", tokenHeader, "", config),
                   404));
    CHECK(statusIs(hcs::rest::handleRequest("POST", "/api/v1/jobs/compute", tokenHeader, "{}",
                                            config),
                   404));
}

HCS_TEST(rest_returns_real_runtime_data)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return;
    }
    const auto config = configWithToken("token");
    const std::string headers = authHeader("token");
    const std::string devices =
        hcs::rest::handleRequest("GET", "/api/v1/devices", headers, "", config);
    CHECK(statusIs(devices, 200));
    CHECK_MSG(devices.find("\"devices\":[") != std::string::npos, devices);
    CHECK_MSG(devices.find("CPU") != std::string::npos, devices);

    const std::string budget =
        hcs::rest::handleRequest("GET", "/api/v1/memory/budget", headers, "", config);
    CHECK(statusIs(budget, 200));
    CHECK_MSG(budget.find("heap_bytes") != std::string::npos, budget);
}

HCS_TEST(rest_serves_self_contained_local_ui)
{
    hcs::rest::RestConfig config;
    config.token = "ui-token";
    const std::string page =
        hcs::rest::handleRequest("GET", "/ui/", "", "", config);
    CHECK(page.rfind("HTTP/1.1 200 ", 0) == 0);
    CHECK_MSG(page.find("Content-Type: text/html") != std::string::npos, page);
    CHECK_MSG(page.find("HCSUDA") != std::string::npos, page);
    CHECK_MSG(page.find("ui-token") != std::string::npos, "token must be embedded for same-origin use");
    CHECK_MSG(page.find("http://") == std::string::npos, "no remote resources");
    CHECK_MSG(page.find("Not exposed by this driver/API") != std::string::npos,
              "unavailable metrics must be marked explicitly");
}

HCS_TEST(rest_settings_roundtrip_and_validation)
{
    hcs::rest::RestConfig config;
    config.token = "token";
    config.settingsDir = (std::filesystem::temp_directory_path() / "hcsuda-rest-settings-test").string();
    std::filesystem::remove_all(config.settingsDir);
    const std::string headers = authHeader("token") + "Content-Type: application/json\r\n";

    const std::string initial = hcs::rest::handleRequest("GET", "/api/v1/settings", headers, "", config);
    CHECK(statusIs(initial, 200));
    CHECK_MSG(initial.find("\"api_port\":8710") != std::string::npos, initial);

    const std::string update = hcs::rest::handleRequest(
        "POST", "/api/v1/settings", headers, "{\"theme\":\"light\",\"cpu_threads\":6}", config);
    CHECK(statusIs(update, 200));
    CHECK_MSG(update.find("\"theme\":\"light\"") != std::string::npos, update);
    CHECK_MSG(update.find("\"cpu_threads\":6") != std::string::npos, update);

    const std::string bad = hcs::rest::handleRequest(
        "POST", "/api/v1/settings", headers, "{\"api_port\":80}", config);
    CHECK(statusIs(bad, 400));

    const std::string reset = hcs::rest::handleRequest(
        "POST", "/api/v1/settings/reset", headers, "", config);
    CHECK(statusIs(reset, 200));
    CHECK_MSG(reset.find("\"theme\":\"dark\"") != std::string::npos, reset);
}
