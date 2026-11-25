#include "api/mcp/mcp_server.h"

#include "core/internal.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

namespace hcs::mcp {

const char* kProtocolVersion = "2025-11-25";

namespace {

std::string escape(const std::string& text)
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

std::string quoted(const std::string& key, const std::string& value)
{
    return "\"" + key + "\":\"" + escape(value) + "\"";
}

/* Extracts the first quoted string following `"key":`. Sufficient for our own request
 * shapes; unusual nesting is rejected by the caller through input-size and depth limits. */
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

std::string idField(const std::string& text)
{
    const size_t start = text.find("\"id\":");
    if (start == std::string::npos) {
        return "null";
    }
    size_t i = start + 5;
    while (i < text.size() && std::isspace(static_cast<unsigned char>(text[i]))) {
        ++i;
    }
    if (i < text.size() && text[i] == '"') {
        return "\"" + escape(stringField(text, "id")) + "\"";
    }
    std::string number;
    for (; i < text.size() && (std::isdigit(static_cast<unsigned char>(text[i])) ||
                               text[i] == '-');
         ++i) {
        number.push_back(text[i]);
    }
    return number.empty() ? "null" : number;
}

std::string reply(const std::string& id, const std::string& result)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"result\":" + result + "}";
}

std::string errorReply(const std::string& id, int code, const std::string& message)
{
    return "{\"jsonrpc\":\"2.0\",\"id\":" + id + ",\"error\":{\"code\":" +
           std::to_string(code) + ",\"message\":\"" + escape(message) + "\"}}";
}

std::string toolsList()
{
    return "{\"tools\":["
           "{\"name\":\"hcsuda_status\",\"description\":\"Runtime version, device count and "
           "actual backend indicators\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}},"
           "{\"name\":\"hcsuda_list_devices\",\"description\":\"Inventory of Vulkan and CPU "
           "devices with capability tiers\",\"inputSchema\":{\"type\":\"object\","
           "\"properties\":{}}},"
           "{\"name\":\"hcsuda_get_memory_budget\",\"description\":\"Reported heaps, budgets "
           "and usage (no invented VRAM size)\",\"inputSchema\":{\"type\":\"object\","
           "\"properties\":{}}},"
           "{\"name\":\"hcsuda_get_diagnostics\",\"description\":\"Sanitized version and "
           "capability report\",\"inputSchema\":{\"type\":\"object\",\"properties\":{}}}"
           "]}";
}

std::string statusPayload()
{
    uint32_t count = 0;
    const hcsResult result = hcsEnumerateDevices(nullptr, &count);
    std::string json = "{";
    json += quoted("product", HCS_PRODUCT_NAME) + ",";
    json += quoted("version", hcsGetVersionString()) + ",";
    json += quoted("api_version", std::to_string(hcsGetApiVersion())) + ",";
    json += "\"device_count\":" + std::to_string(count);
    if (result != HCS_SUCCESS) {
        json += "," + quoted("enumeration_error", hcsResultString(result));
    }
    json += "}";
    return json;
}

std::string devicesPayload()
{
    uint32_t count = 0;
    hcsResult result = hcsEnumerateDevices(nullptr, &count);
    if (result != HCS_SUCCESS || count == 0) {
        return "{\"devices\":[]}";
    }
    std::vector<hcsDeviceInfo> infos(count);
    if (hcsEnumerateDevices(infos.data(), &count) != HCS_SUCCESS) {
        return "{\"devices\":[]}";
    }
    std::string json = "{\"devices\":[";
    for (uint32_t i = 0; i < count; ++i) {
        if (i) {
            json += ",";
        }
        hcsDeviceCaps caps{};
        caps.struct_size = sizeof(caps);
        hcsDevice device = nullptr;
        if (hcsGetDevice(i, &device) == HCS_SUCCESS) {
            hcsGetDeviceCaps(device, &caps);
        }
        json += "{";
        json += quoted("name", infos[i].name) + ",";
        json += quoted("backend", hcsBackendName(infos[i].backend)) + ",";
        json += quoted("kind", hcsDeviceKindName(infos[i].kind)) + ",";
        json += quoted("vendor", infos[i].vendor) + ",";
        json += "\"capabilities\":" + std::to_string(caps.capabilities) + "}";
    }
    json += "]}";
    return json;
}

std::string budgetPayload()
{
    uint32_t count = 0;
    if (hcsEnumerateDevices(nullptr, &count) != HCS_SUCCESS) {
        return "{\"devices\":[]}";
    }
    std::string json = "{\"devices\":[";
    for (uint32_t i = 0; i < count; ++i) {
        hcsDevice device = nullptr;
        if (hcsGetDevice(i, &device) != HCS_SUCCESS) {
            continue;
        }
        hcsMemoryBudget budget{};
        budget.struct_size = sizeof(budget);
        if (hcsGetMemoryBudget(device, &budget) != HCS_SUCCESS) {
            continue;
        }
        if (json.back() != '[') {
            json += ",";
        }
        json += "{\"index\":" + std::to_string(i) +
                ",\"heap_bytes\":" + std::to_string(budget.heap_bytes) +
                ",\"budget_bytes\":" + std::to_string(budget.budget_bytes) +
                ",\"usage_bytes\":" + std::to_string(budget.usage_bytes) +
                ",\"note\":\"driver-reported; not a VRAM size\"}";
    }
    json += "]}";
    return json;
}

bool tooDeep(const std::string& text)
{
    int depth = 0;
    for (char c : text) {
        if (c == '{' || c == '[') {
            if (++depth > 32) {
                return true;
            }
        } else if (c == '}' || c == ']') {
            --depth;
        }
    }
    return false;
}

} // namespace

std::string handle(const std::string& line)
{
    if (line.empty() || line.size() > kMaxMessageBytes) {
        return errorReply("null", -32600, "invalid request size");
    }
    if (tooDeep(line)) {
        return errorReply("null", -32600, "JSON nesting exceeds the limit");
    }
    const std::string method = stringField(line, "method");
    const std::string id = idField(line);
    if (method.empty()) {
        return errorReply(id, -32600, "missing method");
    }

    if (method == "initialize") {
        return reply(id, "{\"protocolVersion\":\"" + std::string(kProtocolVersion) +
                             "\",\"capabilities\":{\"tools\":{}},\"serverInfo\":{" +
                             quoted("name", "hcsbridge-mcp") + "," +
                             quoted("version", hcsGetVersionString()) + "}}");
    }
    if (method == "notifications/initialized") {
        return {}; /* notifications have no reply, ever */
    }
    if (method == "tools/list") {
        return reply(id, toolsList());
    }
    if (method == "tools/call") {
        const std::string name = stringField(line, "name");
        std::string payload;
        if (name == "hcsuda_status") {
            payload = statusPayload();
        } else if (name == "hcsuda_list_devices") {
            payload = devicesPayload();
        } else if (name == "hcsuda_get_memory_budget") {
            payload = budgetPayload();
        } else if (name == "hcsuda_get_diagnostics") {
            payload = statusPayload();
        } else {
            return errorReply(id, -32601, "unknown tool: " + name);
        }
        return reply(id, "{\"content\":[{\"type\":\"text\",\"text\":\"" + escape(payload) +
                             "\"}],\"isError\":false}");
    }
    if (method == "resources/list") {
        return reply(id, "{\"resources\":[]}");
    }
    if (method == "prompts/list") {
        return reply(id, "{\"prompts\":[]}");
    }
    return errorReply(id, -32601, "method not found: " + method);
}

} // namespace hcs::mcp
