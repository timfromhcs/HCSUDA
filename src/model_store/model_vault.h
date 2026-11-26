#pragma once

#include "hcsuda/hcsuda.h"

#include <cstdint>
#include <filesystem>
#include <string>
#include <vector>

namespace hcs::models {

constexpr uint32_t kMaxModels = 512;
constexpr uint32_t kMaxPathChars = 4096;
constexpr uint64_t kMaxModelBytes = uint64_t{64} << 30;

struct ModelEntry {
    std::string id;
    std::string path;
    std::string name;
    std::string format;         /* gguf | onnx | safetensors | pytorch | unknown */
    std::string quantization;   /* filename hint, e.g. q4_k_m, f16, or empty */
    std::string sha256;
    uint64_t size_bytes = 0;
    bool hash_verified = false;
    bool runnable = false;      /* no inference adapter is integrated in V1 */
    std::string note;
};

/*
 * Local model catalog. HDD is treated as persistent storage only: nothing here maps a model
 * into GPU memory or claims execution speed. No network access and no model downloads.
 */
class ModelVault {
public:
    explicit ModelVault(std::filesystem::path root);

    hcsResult initialize();
    hcsResult add(const std::string& userPath, ModelEntry* out);
    hcsResult scan();
    hcsResult list(std::vector<ModelEntry>* out) const;
    hcsResult remove(const std::string& id);

    const std::filesystem::path& root() const { return root_; }

private:
    hcsResult load();
    hcsResult save() const;

    std::filesystem::path root_;
    std::filesystem::path canonicalRoot_;
    std::filesystem::path catalogPath_;
    std::vector<ModelEntry> entries_;
};

} // namespace hcs::models
