#include "model_store/sha256.h"

#include <algorithm>
#include <vector>
#include <cstdio>
#include <cstring>

namespace hcs::models {
namespace {

constexpr uint32_t kConstants[64] = {
    0x428a2f98u, 0x71374491u, 0xb5c0fbcfu, 0xe9b5dba5u, 0x3956c25bu, 0x59f111f1u,
    0x923f82a4u, 0xab1c5ed5u, 0xd807aa98u, 0x12835b01u, 0x243185beu, 0x550c7dc3u,
    0x72be5d74u, 0x80deb1feu, 0x9bdc06a7u, 0xc19bf174u, 0xe49b69c1u, 0xefbe4786u,
    0x0fc19dc6u, 0x240ca1ccu, 0x2de92c6fu, 0x4a7484aau, 0x5cb0a9dcu, 0x76f988dau,
    0x983e5152u, 0xa831c66du, 0xb00327c8u, 0xbf597fc7u, 0xc6e00bf3u, 0xd5a79147u,
    0x06ca6351u, 0x14292967u, 0x27b70a85u, 0x2e1b2138u, 0x4d2c6dfcu, 0x53380d13u,
    0x650a7354u, 0x766a0abbu, 0x81c2c92eu, 0x92722c85u, 0xa2bfe8a1u, 0xa81a664bu,
    0xc24b8b70u, 0xc76c51a3u, 0xd192e819u, 0xd6990624u, 0xf40e3585u, 0x106aa070u,
    0x19a4c116u, 0x1e376c08u, 0x2748774cu, 0x34b0bcb5u, 0x391c0cb3u, 0x4ed8aa4au,
    0x5b9cca4fu, 0x682e6ff3u, 0x748f82eeu, 0x78a5636fu, 0x84c87814u, 0x8cc70208u,
    0x90befffau, 0xa4506cebu, 0xbef9a3f7u, 0xc67178f2u};

inline uint32_t rotr(uint32_t value, uint32_t bits)
{
    return (value >> bits) | (value << (32 - bits));
}

} // namespace

Sha256::Sha256()
{
    state_[0] = 0x6a09e667u;
    state_[1] = 0xbb67ae85u;
    state_[2] = 0x3c6ef372u;
    state_[3] = 0xa54ff53au;
    state_[4] = 0x510e527fu;
    state_[5] = 0x9b05688cu;
    state_[6] = 0x1f83d9abu;
    state_[7] = 0x5be0cd19u;
}

void Sha256::compress(const uint8_t* block)
{
    uint32_t words[64]{};
    for (uint32_t i = 0; i < 16; ++i) {
        words[i] = (static_cast<uint32_t>(block[i * 4]) << 24) |
                   (static_cast<uint32_t>(block[i * 4 + 1]) << 16) |
                   (static_cast<uint32_t>(block[i * 4 + 2]) << 8) |
                   static_cast<uint32_t>(block[i * 4 + 3]);
    }
    for (uint32_t i = 16; i < 64; ++i) {
        const uint32_t s0 = rotr(words[i - 15], 7) ^ rotr(words[i - 15], 18) ^
                            (words[i - 15] >> 3);
        const uint32_t s1 = rotr(words[i - 2], 17) ^ rotr(words[i - 2], 19) ^
                            (words[i - 2] >> 10);
        words[i] = words[i - 16] + s0 + words[i - 7] + s1;
    }

    uint32_t a = state_[0], b = state_[1], c = state_[2], d = state_[3];
    uint32_t e = state_[4], f = state_[5], g = state_[6], h = state_[7];
    for (uint32_t i = 0; i < 64; ++i) {
        const uint32_t s1 = rotr(e, 6) ^ rotr(e, 11) ^ rotr(e, 25);
        const uint32_t choice = (e & f) ^ (~e & g);
        const uint32_t t1 = h + s1 + choice + kConstants[i] + words[i];
        const uint32_t s0 = rotr(a, 2) ^ rotr(a, 13) ^ rotr(a, 22);
        const uint32_t majority = (a & b) ^ (a & c) ^ (b & c);
        const uint32_t t2 = s0 + majority;
        h = g;
        g = f;
        f = e;
        e = d + t1;
        d = c;
        c = b;
        b = a;
        a = t1 + t2;
    }
    state_[0] += a;
    state_[1] += b;
    state_[2] += c;
    state_[3] += d;
    state_[4] += e;
    state_[5] += f;
    state_[6] += g;
    state_[7] += h;
}

void Sha256::update(const void* data, size_t size)
{
    if (finished_ || size == 0) {
        return;
    }
    const uint8_t* bytes = static_cast<const uint8_t*>(data);
    total_ += size;
    if (buffered_ > 0) {
        const size_t take = std::min<size_t>(64 - buffered_, size);
        std::memcpy(buffer_ + buffered_, bytes, take);
        buffered_ += take;
        bytes += take;
        size -= take;
        if (buffered_ == 64) {
            compress(buffer_);
            buffered_ = 0;
        }
    }
    while (size >= 64) {
        compress(bytes);
        bytes += 64;
        size -= 64;
    }
    if (size > 0) {
        std::memcpy(buffer_, bytes, size);
        buffered_ = size;
    }
}

std::string Sha256::finish()
{
    if (finished_) {
        return {};
    }
    const uint64_t bitLength = total_ * 8;
    const uint8_t padding = 0x80;
    update(&padding, 1);
    const uint8_t zero = 0;
    while (buffered_ != 56) {
        update(&zero, 1);
    }
    uint8_t length[8]{};
    for (int i = 0; i < 8; ++i) {
        length[7 - i] = static_cast<uint8_t>((bitLength >> (i * 8)) & 0xFF);
    }
    update(length, 8);
    finished_ = true;

    char hex[65]{};
    for (uint32_t i = 0; i < 8; ++i) {
        std::snprintf(hex + i * 8, 9, "%08x", state_[i]);
    }
    return std::string(hex, 64);
}

std::string Sha256::hashFile(const std::string& path, uint64_t* bytesRead)
{
    std::FILE* file = std::fopen(path.c_str(), "rb");
    if (!file) {
        return {};
    }
    Sha256 hash;
    /* Heap buffer: a 64 KiB stack array trips MSVC /analyze C6262. */
    std::vector<uint8_t> chunk(64 * 1024);
    uint64_t total = 0;
    size_t read = 0;
    while ((read = std::fread(chunk.data(), 1, chunk.size(), file)) > 0) {
        hash.update(chunk.data(), read);
        total += read;
    }
    const bool ok = std::ferror(file) == 0;
    std::fclose(file);
    if (bytesRead) {
        *bytesRead = total;
    }
    return ok ? hash.finish() : std::string();
}

} // namespace hcs::models
