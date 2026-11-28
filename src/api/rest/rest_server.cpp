#include "api/rest/rest_server.h"

#include "api/mcp/mcp_server.h"
#include "core/internal.h"
#include "api/rest/ui_page.h"
#include "service/settings.h"
#include "model_store/model_vault.h"
#include "model_store/sha256.h"

#include <cctype>
#include <filesystem>
#include <random>
#include <vector>

namespace hcs::rest {
namespace {

std::string escapeJson(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (static_cast<unsigned char>(c) < 0x20) {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string errorBody(int status, const char* code, const std::string& message)
{
    return "{\"error\":{\"status\":" + std::to_string(status) + ",\"code\":\"" + code +
           "\",\"message\":\"" + message + "\"}}";
}

std::string response(int status, const std::string& body)
{
    const char* reason = status == 200   ? "OK"
                         : status == 400 ? "Bad Request"
                         : status == 401 ? "Unauthorized"
                         : status == 403 ? "Forbidden"
                         : status == 404 ? "Not Found"
                         : status == 413 ? "Payload Too Large"
                         : status == 429 ? "Too Many Requests"
                                         : "Internal Server Error";
    std::string out = "HTTP/1.1 " + std::to_string(status) + " " + reason + "\r\n";
    out += "Content-Type: application/json; charset=utf-8\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Connection: close\r\n\r\n";
    out += body;
    return out;
}

std::string htmlResponse(const std::string& body)
{
    std::string out = "HTTP/1.1 200 OK\r\n";
    out += "Content-Type: text/html; charset=utf-8\r\n";
    out += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    out += "X-Content-Type-Options: nosniff\r\n";
    out += "Cache-Control: no-store\r\n";
    out += "Connection: close\r\n\r\n";
    return out + body;
}

std::string headerValue(const std::string& headers, const std::string& name)
{
    const std::string needle = name + ":";
    size_t lineStart = 0;
    while (lineStart < headers.size()) {
        const size_t lineEnd = headers.find("\r\n", lineStart);
        const size_t stop = lineEnd == std::string::npos ? headers.size() : lineEnd;
        std::string line = headers.substr(lineStart, stop - lineStart);
        size_t i = 0;
        while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
            ++i;
        }
        if (line.compare(i, needle.size(), needle) == 0) {
            i += needle.size();
            while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
                ++i;
            }
            return line.substr(i);
        }
        if (lineEnd == std::string::npos) {
            break;
        }
        lineStart = lineEnd + 2;
    }
    return {};
}

bool originAllowed(const std::string& origin)
{
    if (origin.empty()) {
        return true; /* non-browser clients have no Origin header */
    }
    return origin == "http://localhost" || origin.find("http://localhost:") == 0 ||
           origin == "http://127.0.0.1" || origin.find("http://127.0.0.1:") == 0 ||
           origin == "http://[::1]" || origin.find("http://[::1]:") == 0;
}

std::string devicesJson()
{
    uint32_t count = 0;
    if (hcsEnumerateDevices(nullptr, &count) != HCS_SUCCESS || count == 0) {
        return "{\"devices\":[]}";
    }
    std::vector<hcsDeviceInfo> infos(count);
    if (hcsEnumerateDevices(infos.data(), &count) != HCS_SUCCESS) {
        return "{\"devices\":[]}";
    }
    std::string json = "{\"devices\":[";
    for (uint32_t i = 0; i < count; ++i) {
        if (i) json += ",";
        json += "{\"index\":" + std::to_string(i) + ",\"name\":\"" +
                std::string(infos[i].name) + "\",\"backend\":\"" +
                hcsBackendName(infos[i].backend) + "\",\"kind\":\"" +
                hcsDeviceKindName(infos[i].kind) + "\"}";
    }
    return json + "]}";
}

std::string budgetsJson()
{
    uint32_t count = 0;
    if (hcsEnumerateDevices(nullptr, &count) != HCS_SUCCESS) {
        return "{\"devices\":[]}";
    }
    std::string json = "{\"devices\":[";
    bool first = true;
    for (uint32_t i = 0; i < count; ++i) {
        hcsDevice device = nullptr;
        if (hcsGetDevice(i, &device) != HCS_SUCCESS) continue;
        hcsMemoryBudget budget{};
        budget.struct_size = sizeof(budget);
        if (hcsGetMemoryBudget(device, &budget) != HCS_SUCCESS) continue;
        if (!first) json += ",";
        first = false;
        json += "{\"index\":" + std::to_string(i) + ",\"heap_bytes\":" +
                std::to_string(budget.heap_bytes) + ",\"budget_bytes\":" +
                std::to_string(budget.budget_bytes) + ",\"usage_bytes\":" +
                std::to_string(budget.usage_bytes) + "}";
    }
    return json + "]}";
}

} // namespace

std::string generateToken()
{
    std::random_device device;
    unsigned char seed[64];
    for (auto& byte : seed) {
        byte = static_cast<unsigned char>(device());
    }
    hcs::models::Sha256 hash;
    hash.update(seed, sizeof(seed));
    return hash.finish(); /* 64 hex characters, unique per process */
}

bool rateLimitAllows(RateLimitState* state, std::chrono::steady_clock::time_point now)
{
    if (!state) {
        return true;
    }
    const auto window = std::chrono::seconds(kRateLimitWindowSeconds);
    if (state->windowStart == std::chrono::steady_clock::time_point{} ||
        now - state->windowStart >= window) {
        state->windowStart = now;
        state->count = 0;
    }
    if (state->count >= kRateLimitMaxRequests) {
        return false;
    }
    ++state->count;
    return true;
}

bool constantTimeEquals(const std::string& a, const std::string& b)
{
    if (a.size() != b.size()) {
        return false;
    }
    unsigned char difference = 0;
    for (size_t i = 0; i < a.size(); ++i) {
        difference |= static_cast<unsigned char>(a[i]) ^ static_cast<unsigned char>(b[i]);
    }
    return difference == 0;
}

std::string handleRequest(const std::string& method, const std::string& target,
                          const std::string& headers, const std::string& body,
                          const RestConfig& config, RateLimitState* limiter)
{
    if (!originAllowed(headerValue(headers, "Origin"))) {
        return response(403, errorBody(403, "security_policy", "cross-origin request blocked"));
    }
    if (body.size() > kMaxRequestBytes) {
        return response(413, errorBody(413, "payload_too_large", "request body exceeds 64 KiB"));
    }
    if (target.find("..") != std::string::npos || target.find('\\') != std::string::npos) {
        return response(400, errorBody(400, "invalid_path", "path traversal in request target"));
    }

    const std::string path = target.substr(0, target.find('?'));
    /* Health stays available for monitoring even when the budget is exhausted. */
    if (path != "/api/v1/health" &&
        !rateLimitAllows(limiter, std::chrono::steady_clock::now())) {
        return response(429, errorBody(429, "rate_limited",
                                       "request limit reached; retry after the current window"));
    }
    if (method == "GET" && (path == "/" || path == "/ui/" || path == "/ui")) {
        return htmlResponse(uiPage(config.token));
    }
    if (method == "GET" && path == "/api/v1/health") {
        return response(200, "{\"status\":\"ok\",\"local_only\":true}");
    }
    if (!config.token.empty()) {
        const std::string authorization = headerValue(headers, "Authorization");
        const std::string prefix = "Bearer ";
        if (authorization.compare(0, prefix.size(), prefix) != 0 ||
            !constantTimeEquals(authorization.substr(prefix.size()), config.token)) {
            return response(401, errorBody(401, "unauthorized", "valid bearer token required"));
        }
    }

    if (method == "GET" && path == "/api/v1/settings") {
        hcs::service::SettingsStore store(config.settingsDir.empty()
                                              ? std::filesystem::path()
                                              : std::filesystem::path(config.settingsDir));
        (void)store.load();
        return response(200, store.toJson());
    }
    if (method == "POST" && (path == "/api/v1/settings" || path == "/api/v1/settings/reset")) {
        hcs::service::SettingsStore store(config.settingsDir.empty()
                                              ? std::filesystem::path()
                                              : std::filesystem::path(config.settingsDir));
        (void)store.load();
        const hcsResult result = path.ends_with("/reset") ? store.reset()
                                                          : store.updateFromJson(body);
        if (result != HCS_SUCCESS) {
            return response(400, errorBody(400, "invalid_settings", hcsResultString(result)));
        }
        return response(200, store.toJson());
    }
    if (method == "GET" && path == "/api/v1/models") {
        hcs::service::SettingsStore settings(config.settingsDir.empty()
                                                 ? std::filesystem::path()
                                                 : std::filesystem::path(config.settingsDir));
        (void)settings.load();
        const std::string& root = settings.get().modelDir;
        std::string json = "{\"configured\":" + std::string(root.empty() ? "false" : "true") +
                           ",\"models\":[";
        if (!root.empty()) {
            hcs::models::ModelVault vault(root);
            std::vector<hcs::models::ModelEntry> entries;
            if (vault.list(&entries) == HCS_SUCCESS) {
                for (size_t i = 0; i < entries.size(); ++i) {
                    if (i) json += ",";
                    const auto& entry = entries[i];
                    json += "{\"id\":\"" + escapeJson(entry.id) + "\",\"name\":\"" +
                            escapeJson(entry.name) + "\",\"format\":\"" +
                            escapeJson(entry.format) + "\",\"size_bytes\":" +
                            std::to_string(entry.size_bytes) + ",\"hash_verified\":" +
                            (entry.hash_verified ? "true" : "false") + ",\"runnable\":false}";
                }
            }
        }
        return response(200, json + "]}");
    }
    if (method != "GET") {
        return response(404, errorBody(404, "not_found",
                                       "no such endpoint; arbitrary code execution is not exposed"));
    }
    if (path == "/api/v1/version") {
        return response(200, "{\"product\":\"" + std::string(HCS_PRODUCT_NAME) +
                                 "\",\"version\":\"" + hcsGetVersionString() +
                                 "\",\"api_version\":" + std::to_string(hcsGetApiVersion()) +
                                 "}");
    }
    if (path == "/api/v1/devices" || path == "/api/v1/runtime/status") {
        return response(200, devicesJson());
    }
    if (path == "/api/v1/memory/budget") {
        return response(200, budgetsJson());
    }
    return response(404, errorBody(404, "not_found", "unknown endpoint"));
}

} // namespace hcs::rest
