#pragma once

#include <chrono>
#include <cstdint>
#include <string>

namespace hcs::rest {

constexpr uint32_t kMaxRequestBytes = 64u << 10;
constexpr uint32_t kRateLimitMaxRequests = 120;
constexpr int kRateLimitWindowSeconds = 10;

/* Fixed-window request counter owned by the server process (not by a connection). */
struct RateLimitState {
    std::chrono::steady_clock::time_point windowStart{};
    uint32_t count = 0;
};

/* Returns false when the window budget is exhausted. A null state means "unlimited",
 * which keeps the pure handleRequest helper usable in tests without shared state. */
bool rateLimitAllows(RateLimitState* state, std::chrono::steady_clock::time_point now);

struct RestConfig {
    uint16_t port = 8710;
    std::string token;
    std::string bindAddress = "127.0.0.1";
    std::string settingsDir; /* empty = %LOCALAPPDATA%\HCSUDA */
};

/* Cryptographically random per-process token (hex, 64 chars). Never a hardcoded default. */
std::string generateToken();

/* Constant-time comparison for equal-length inputs. */
bool constantTimeEquals(const std::string& a, const std::string& b);

/* Returns a complete HTTP/1.1 response. Pure and independently testable; the socket layer
 * only reads a bounded request and writes the returned bytes. */
std::string handleRequest(const std::string& method, const std::string& target,
                          const std::string& headers, const std::string& body,
                          const RestConfig& config, RateLimitState* limiter = nullptr);

} // namespace hcs::rest
