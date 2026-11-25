#pragma once

#include "hcsuda/hcsuda.h"

#include <string>

namespace hcs::mcp {

constexpr uint32_t kMaxMessageBytes = 1u << 20;
extern const char* kProtocolVersion;

/* Processes exactly one JSON-RPC message. A notification returns an empty string.
 * Pure function of its input plus the already-initialized runtime, which makes the wire
 * protocol testable without spawning a process. */
std::string handle(const std::string& line);

} // namespace hcs::mcp
