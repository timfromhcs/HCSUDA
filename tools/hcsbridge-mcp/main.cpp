/*
 * HCSBridge MCP server — stdio transport.
 *
 * Only valid JSON-RPC messages are written to stdout. All diagnostics go to stderr.
 * The tool set is deliberately read-only and contains no shell, file-read or download tool.
 */

#include "api/mcp/mcp_server.h"

#include <cstdio>
#include <iostream>
#include <string>

int main()
{
    const hcsResult init = hcsInit(nullptr);
    if (init != HCS_SUCCESS) {
        std::fprintf(stderr, "hcsInit failed: %s\n", hcsResultString(init));
    }

    std::string line;
    while (std::getline(std::cin, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        const std::string answer = hcs::mcp::handle(line);
        if (!answer.empty()) {
            std::cout << answer << '\n';
            std::cout.flush();
        }
    }

    if (init == HCS_SUCCESS) {
        hcsShutdown();
    }
    return 0;
}
