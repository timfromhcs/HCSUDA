#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace hcs::models {

/* Small self-contained SHA-256 (FIPS 180-4). No external crypto dependency. */
class Sha256 {
public:
    Sha256();
    void update(const void* data, size_t size);
    std::string finish(); /* lowercase hex, 64 characters */
    static std::string hashFile(const std::string& path, uint64_t* bytesRead);

private:
    void compress(const uint8_t* block);

    uint32_t state_[8]{};
    uint8_t buffer_[64]{};
    size_t buffered_ = 0;
    uint64_t total_ = 0;
    bool finished_ = false;
};

} // namespace hcs::models
