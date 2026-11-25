/*
 * HCSBridge REST — local-only HTTP service.
 *
 * Binds to 127.0.0.1 only. No remote binding option exists in V1. A random bearer token is
 * generated per process and printed to stderr, never to stdout or a log file in the repo.
 * Requires a running HCS runtime; all responses come from real API queries.
 */

#include "api/rest/rest_server.h"
#include "hcsuda/hcsuda.h"

#include <winsock2.h>
#include <ws2tcpip.h>

#include <csignal>
#include <cstdio>
#include <cstring>
#include <string>

namespace {

volatile sig_atomic_t g_stop = 0;

void onSignal(int) { g_stop = 1; }

bool sendAll(SOCKET socket, const std::string& bytes)
{
    size_t sent = 0;
    while (sent < bytes.size()) {
        const int result = ::send(socket, bytes.data() + sent,
                                  static_cast<int>(bytes.size() - sent), 0);
        if (result <= 0) {
            return false;
        }
        sent += static_cast<size_t>(result);
    }
    return true;
}

} // namespace

int main(int argc, char** argv)
{
    hcs::rest::RestConfig config;
    for (int i = 1; i < argc; ++i) {
        if (std::strcmp(argv[i], "--port") == 0 && i + 1 < argc) {
            const int port = std::atoi(argv[++i]);
            if (port < 1024 || port > 65535) {
                std::fprintf(stderr, "port must be 1024..65535\n");
                return 2;
            }
            config.port = static_cast<uint16_t>(port);
        } else if (std::strcmp(argv[i], "--token") == 0 && i + 1 < argc) {
            config.token = argv[++i];
            if (config.token.size() != 64) {
                std::fprintf(stderr, "token must be 64 hex characters\n");
                return 2;
            }
        } else if (std::strcmp(argv[i], "--help") == 0) {
            std::printf("usage: hcsbridge-rest [--port N] [--token 64-hex]\n");
            return 0;
        }
    }
    if (config.token.empty()) {
        config.token = hcs::rest::generateToken();
    }
    if (hcsInit(nullptr) != HCS_SUCCESS) {
        std::fprintf(stderr, "hcsInit failed\n");
        return 1;
    }

    WSADATA wsa{};
    if (WSAStartup(MAKEWORD(2, 2), &wsa) != 0) {
        std::fprintf(stderr, "WSAStartup failed\n");
        hcsShutdown();
        return 1;
    }
    const SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET) {
        std::fprintf(stderr, "socket failed\n");
        WSACleanup();
        hcsShutdown();
        return 1;
    }
    int reuse = 1;
    ::setsockopt(listener, SOL_SOCKET, SO_REUSEADDR,
                 reinterpret_cast<const char*>(&reuse), sizeof(reuse));

    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_port = htons(config.port);
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK); /* 127.0.0.1: no remote binding */
    if (::bind(listener, reinterpret_cast<sockaddr*>(&address), sizeof(address)) ==
            SOCKET_ERROR ||
        ::listen(listener, 8) == SOCKET_ERROR) {
        std::fprintf(stderr, "bind/listen failed on 127.0.0.1:%u\n", config.port);
        closesocket(listener);
        WSACleanup();
        hcsShutdown();
        return 1;
    }

    std::signal(SIGINT, onSignal);
    std::signal(SIGTERM, onSignal);
    std::fprintf(stderr, "hcsbridge-rest listening on http://127.0.0.1:%u (local only)\n",
                 config.port);
    std::fprintf(stderr, "bearer token: %s\n", config.token.c_str());

    while (!g_stop) {
        const SOCKET client = ::accept(listener, nullptr, nullptr);
        if (client == INVALID_SOCKET) {
            break;
        }
        std::string request;
        char chunk[4096];
        size_t headerEnd = std::string::npos;
        size_t contentLength = 0;
        while (request.size() <= hcs::rest::kMaxRequestBytes) {
            const int read = ::recv(client, chunk, sizeof(chunk), 0);
            if (read <= 0) {
                break;
            }
            request.append(chunk, static_cast<size_t>(read));
            if (headerEnd == std::string::npos) {
                headerEnd = request.find("\r\n\r\n");
                if (headerEnd != std::string::npos) {
                    const std::string headers = request.substr(0, headerEnd);
                    const size_t marker = headers.find("Content-Length:");
                    if (marker != std::string::npos) {
                        contentLength = std::strtoul(headers.c_str() + marker + 15, nullptr, 10);
                    }
                }
            }
            if (headerEnd != std::string::npos &&
                request.size() >= headerEnd + 4 + contentLength) {
                break;
            }
        }

        std::string method, target, headers, body;
        if (headerEnd != std::string::npos) {
            headers = request.substr(0, headerEnd);
            const size_t firstLineEnd = headers.find("\r\n");
            const std::string firstLine = headers.substr(0, firstLineEnd);
            const size_t space = firstLine.find(' ');
            if (space != std::string::npos) {
                method = firstLine.substr(0, space);
                const size_t second = firstLine.find(' ', space + 1);
                target = firstLine.substr(space + 1, second == std::string::npos
                                                         ? std::string::npos
                                                         : second - space - 1);
            }
            const size_t bodyStart = headerEnd + 4;
            if (request.size() > bodyStart) {
                body = request.substr(bodyStart);
            }
        }
        const std::string answer = hcs::rest::handleRequest(method, target, headers, body, config);
        sendAll(client, answer);
        closesocket(client);
    }

    closesocket(listener);
    WSACleanup();
    hcsShutdown();
    return 0;
}
