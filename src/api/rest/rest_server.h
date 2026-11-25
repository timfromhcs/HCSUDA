#pragma once

#include <cstdint>
#include <string>

namespace hcs::rest {

constexpr uint32_t kMaxRequestBytes = 64u << 10;

struct RestConfig {
    uint16_t port = 8710;
    std::string token;
    std::string bindAddress = "127.0.0.1";
};

/* Cryptographically random per-process token (hex, 64 chars). Never a hardcoded default. */
std::string generateToken();

/* Constant-time comparison for equal-length inputs. */
bool constantTimeEquals(const std::string& a, const std::string& b);

/* Returns a complete HTTP/1.1 response. Pure and independently testable; the socket layer
 * only reads a bounded request and writes the returned bytes. */
std::string handleRequest(const std::string& method, const std::string& target,
                          const std::string& headers, const std::string& body,
                          const RestConfig& config);

} // namespace hcs::rest
