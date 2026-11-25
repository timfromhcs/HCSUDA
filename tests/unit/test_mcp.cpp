#include "test_framework.h"
#include "test_helpers.h"

#include "api/mcp/mcp_server.h"

#include <string>

namespace {

std::string call(const std::string& method)
{
    return hcs::mcp::handle("{\"jsonrpc\":\"2.0\",\"id\":7,\"method\":\"" + method + "\"}");
}

template <typename F>
bool withSession(F&& fn)
{
    testutil::DeviceSession session;
    if (!session.open(HCS_BACKEND_CPU)) {
        hcstest::reportSkip("no CPU device available");
        return true;
    }
    fn();
    return true;
}

} // namespace

HCS_TEST(mcp_initialize_negotiates_protocol)
{
    const std::string answer = call("initialize");
    CHECK_MSG(answer.find("\"jsonrpc\":\"2.0\"") != std::string::npos, answer);
    CHECK_MSG(answer.find("\"id\":7") != std::string::npos, answer);
    CHECK_MSG(answer.find(std::string("\"protocolVersion\":\"") + hcs::mcp::kProtocolVersion) !=
                  std::string::npos,
              answer);
    CHECK_MSG(answer.find("\"capabilities\":{\"tools\":{}}") != std::string::npos, answer);
}

HCS_TEST(mcp_notification_has_no_reply)
{
    CHECK(hcs::mcp::handle("{\"jsonrpc\":\"2.0\",\"method\":\"notifications/initialized\"}")
              .empty());
}

HCS_TEST(mcp_tools_list_exposes_only_safe_read_only_tools)
{
    const std::string answer = call("tools/list");
    CHECK_MSG(answer.find("hcsuda_status") != std::string::npos, answer);
    CHECK_MSG(answer.find("hcsuda_list_devices") != std::string::npos, answer);
    CHECK_MSG(answer.find("hcsuda_get_memory_budget") != std::string::npos, answer);
    CHECK_MSG(answer.find("hcsuda_get_diagnostics") != std::string::npos, answer);
    CHECK_MSG(answer.find("run_command") == std::string::npos, "no shell tool allowed");
    CHECK_MSG(answer.find("read_file") == std::string::npos, "no arbitrary file read");
    CHECK_MSG(answer.find("download") == std::string::npos, "no download tool");
}

HCS_TEST(mcp_tools_call_returns_real_runtime_data)
{
    withSession([]() {
        const std::string answer = hcs::mcp::handle(
            "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"tools/call\",\"params\":"
            "{\"name\":\"hcsuda_status\",\"arguments\":{}}}");
        CHECK_MSG(answer.find("\"isError\":false") != std::string::npos, answer);
        CHECK_MSG(answer.find(HCS_PRODUCT_NAME) != std::string::npos, answer);
        CHECK_MSG(answer.find("device_count") != std::string::npos, answer);

        const std::string devices = hcs::mcp::handle(
            "{\"jsonrpc\":\"2.0\",\"id\":2,\"method\":\"tools/call\",\"params\":"
            "{\"name\":\"hcsuda_list_devices\",\"arguments\":{}}}");
        CHECK_MSG(devices.find("CPU") != std::string::npos, devices);

        const std::string budgets = hcs::mcp::handle(
            "{\"jsonrpc\":\"2.0\",\"id\":3,\"method\":\"tools/call\",\"params\":"
            "{\"name\":\"hcsuda_get_memory_budget\",\"arguments\":{}}}");
        CHECK_MSG(budgets.find("heap_bytes") != std::string::npos, budgets);
    });
}

HCS_TEST(mcp_rejects_unknown_methods_and_tools)
{
    CHECK_MSG(call("no/such/method").find("\"code\":-32601") != std::string::npos,
              "unknown method must be -32601");
    const std::string unknownTool = hcs::mcp::handle(
        "{\"jsonrpc\":\"2.0\",\"id\":9,\"method\":\"tools/call\",\"params\":"
        "{\"name\":\"run_command\",\"arguments\":{}}}");
    CHECK_MSG(unknownTool.find("\"code\":-32601") != std::string::npos, unknownTool);
    CHECK_MSG(unknownTool.find("unknown tool") != std::string::npos, unknownTool);
}

HCS_TEST(mcp_rejects_malformed_and_oversized_messages)
{
    CHECK_MSG(hcs::mcp::handle("").find("\"code\":-32600") != std::string::npos,
              "empty request must be rejected");
    CHECK_MSG(hcs::mcp::handle("{}").find("\"code\":-32600") != std::string::npos,
              "missing method must be rejected");
    std::string deep = "{\"method\":\"";
    deep.append(64, 'x');
    deep += "\"}";
    std::string nested(128, '{');
    nested += deep;
    nested.append(128, '}');
    CHECK_MSG(hcs::mcp::handle(nested).find("-32600") != std::string::npos,
              "deep nesting must be rejected before dispatch");
    std::string huge(hcs::mcp::kMaxMessageBytes + 1, 'x');
    CHECK_MSG(hcs::mcp::handle(huge).find("-32600") != std::string::npos,
              "oversized message must be rejected");
}
