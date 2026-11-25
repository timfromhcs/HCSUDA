#include "model_store/model_vault.h"

#include "core/internal.h"
#include "model_store/sha256.h"

#include <algorithm>
#include <cctype>
#include <cstdio>
#include <cstring>
#include <cstdlib>
#include <fstream>
#include <sstream>

namespace hcs::models {
namespace {

std::string escape(const std::string& text)
{
    std::string out;
    for (char c : text) {
        if (c == '"' || c == '\\') {
            out.push_back('\\');
            out.push_back(c);
        } else if (c == '\n' || c == '\r') {
            out.push_back(' ');
        } else {
            out.push_back(c);
        }
    }
    return out;
}

std::string quotedValue(const std::string& line, const std::string& key)
{
    const std::string needle = "\"" + key + "\":";
    const size_t start = line.find(needle);
    if (start == std::string::npos) {
        return {};
    }
    size_t i = start + needle.size();
    while (i < line.size() && std::isspace(static_cast<unsigned char>(line[i]))) {
        ++i;
    }
    if (i >= line.size() || line[i] != '"') {
        return {};
    }
    ++i;
    std::string value;
    for (; i < line.size(); ++i) {
        if (line[i] == '\\' && i + 1 < line.size()) {
            value.push_back(line[++i]);
        } else if (line[i] == '"') {
            break;
        } else {
            value.push_back(line[i]);
        }
    }
    return value;
}

std::string lower(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(), [](unsigned char c) {
        return static_cast<char>(std::tolower(c));
    });
    return text;
}

std::string quantizationHint(const std::string& filename)
{
    const std::string name = lower(filename);
    static const char* hints[] = {"q4_k_m", "q4_k_s", "q5_k_m", "q6_k", "q8_0",
                                  "q4_0",   "q4_1",   "iq2_xxs", "f16",  "f32"};
    for (const char* hint : hints) {
        if (name.find(hint) != std::string::npos) {
            return hint;
        }
    }
    return {};
}

std::string formatFor(const std::filesystem::path& path, const std::string& sha256)
{
    (void)sha256;
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (!file) {
        return "unknown";
    }
    char magic[4] = {};
    const size_t read = std::fread(magic, 1, sizeof(magic), file);
    std::fclose(file);
    if (read == 4 && std::memcmp(magic, "GGUF", 4) == 0) {
        return "gguf";
    }
    const std::string ext = lower(path.extension().string());
    if (ext == ".onnx") {
        return "onnx";
    }
    if (ext == ".safetensors") {
        return "safetensors";
    }
    if (ext == ".pt" || ext == ".pth" || ext == ".bin") {
        return "pytorch";
    }
    return "unknown";
}

bool ggufLooksValid(const std::filesystem::path& path)
{
    std::FILE* file = std::fopen(path.string().c_str(), "rb");
    if (!file) {
        return false;
    }
    unsigned char header[8] = {};
    const size_t read = std::fread(header, 1, sizeof(header), file);
    std::fclose(file);
    if (read != 8 || std::memcmp(header, "GGUF", 4) != 0) {
        return false;
    }
    const uint32_t version = static_cast<uint32_t>(header[4]) |
                             (static_cast<uint32_t>(header[5]) << 8) |
                             (static_cast<uint32_t>(header[6]) << 16) |
                             (static_cast<uint32_t>(header[7]) << 24);
    return version == 2 || version == 3;
}

bool insideRoot(const std::filesystem::path& candidate, const std::filesystem::path& root)
{
    const std::filesystem::path relative = candidate.lexically_relative(root);
    if (relative.empty()) {
        return false;
    }
    const std::string text = relative.generic_string();
    return text != ".." && text.find("../") != 0;
}

std::string entryLine(const ModelEntry& entry)
{
    std::string line = "{";
    line += "\"id\":\"" + escape(entry.id) + "\",";
    line += "\"path\":\"" + escape(entry.path) + "\",";
    line += "\"name\":\"" + escape(entry.name) + "\",";
    line += "\"format\":\"" + escape(entry.format) + "\",";
    line += "\"quantization\":\"" + escape(entry.quantization) + "\",";
    line += "\"sha256\":\"" + escape(entry.sha256) + "\",";
    line += "\"size_bytes\":\"" + std::to_string(entry.size_bytes) + "\",";
    line += "\"hash_verified\":" + std::string(entry.hash_verified ? "true" : "false") + ",";
    line += "\"runnable\":" + std::string(entry.runnable ? "true" : "false") + ",";
    line += "\"note\":\"" + escape(entry.note) + "\"}";
    return line;
}

} // namespace

ModelVault::ModelVault(std::filesystem::path root)
    : root_(std::move(root)), catalogPath_(root_ / "catalog.jsonl")
{
}

hcsResult ModelVault::initialize()
{
    std::error_code error;
    std::filesystem::create_directories(root_, error);
    if (error) {
        return fail(HCS_E_IO, "models.initialize", "cannot create model directory: " +
                                                        error.message());
    }
    return load();
}

hcsResult ModelVault::load()
{
    entries_.clear();
    std::ifstream file(catalogPath_, std::ios::binary);
    if (!file) {
        return HCS_SUCCESS; /* first run */
    }
    std::string line;
    while (std::getline(file, line)) {
        if (line.empty() || entries_.size() >= kMaxModels) {
            break;
        }
        ModelEntry entry;
        entry.id = quotedValue(line, "id");
        entry.path = quotedValue(line, "path");
        entry.name = quotedValue(line, "name");
        entry.format = quotedValue(line, "format");
        entry.quantization = quotedValue(line, "quantization");
        entry.sha256 = quotedValue(line, "sha256");
        entry.note = quotedValue(line, "note");
        const std::string size = quotedValue(line, "size_bytes");
        entry.size_bytes = size.empty() ? 0 : std::strtoull(size.c_str(), nullptr, 10);
        entry.hash_verified = line.find("\"hash_verified\":true") != std::string::npos;
        entry.runnable = line.find("\"runnable\":true") != std::string::npos;
        if (!entry.id.empty()) {
            entries_.push_back(std::move(entry));
        }
    }
    return HCS_SUCCESS;
}

hcsResult ModelVault::save() const
{
    const std::filesystem::path temp = catalogPath_.string() + ".tmp";
    {
        std::ofstream file(temp, std::ios::binary | std::ios::trunc);
        if (!file) {
            return fail(HCS_E_IO, "models.save", "cannot write catalog temporary file");
        }
        for (const auto& entry : entries_) {
            file << entryLine(entry) << '\n';
        }
        file.flush();
        if (!file) {
            return fail(HCS_E_IO, "models.save", "catalog write failed");
        }
    }
    std::error_code error;
    std::filesystem::remove(catalogPath_, error);
    std::filesystem::rename(temp, catalogPath_, error);
    if (error) {
        return fail(HCS_E_IO, "models.save", "cannot finalize catalog: " + error.message());
    }
    return HCS_SUCCESS;
}

hcsResult ModelVault::add(const std::string& userPath, ModelEntry* out)
{
    if (userPath.empty() || userPath.size() > kMaxPathChars || !out) {
        return fail(HCS_E_INVALID_ARGUMENT, "models.add", "path or output is invalid");
    }
    std::error_code error;
    const std::filesystem::path input(userPath);
    if (input.filename().string().find("..") != std::string::npos) {
        return fail(HCS_E_SECURITY_POLICY, "models.add", "path traversal in filename");
    }
    const std::filesystem::path canonical = std::filesystem::weakly_canonical(input, error);
    if (error) {
        return fail(HCS_E_IO, "models.add", "cannot resolve path: " + error.message());
    }
    if (!insideRoot(canonical, root_)) {
        return fail(HCS_E_SECURITY_POLICY, "models.add",
                    "model path is outside the configured model directory");
    }
    if (!std::filesystem::is_regular_file(canonical, error) || error) {
        return fail(HCS_E_IO, "models.add", "model path is not a regular file");
    }
    const uint64_t size = std::filesystem::file_size(canonical, error);
    if (error || size == 0 || size > kMaxModelBytes) {
        return fail(HCS_E_IO, "models.add", "model size is invalid or exceeds 64 GiB");
    }

    ModelEntry entry;
    entry.path = canonical.generic_string();
    entry.name = canonical.filename().string();
    entry.size_bytes = size;
    entry.sha256 = Sha256::hashFile(entry.path, nullptr);
    if (entry.sha256.size() != 64) {
        return fail(HCS_E_IO, "models.add", "cannot hash model file");
    }
    entry.id = entry.sha256.substr(0, 16);
    entry.format = formatFor(canonical, entry.sha256);
    entry.quantization = quantizationHint(entry.name);
    entry.hash_verified = true;
    entry.runnable = false; /* no tested inference adapter in V1 */
    entry.note = entry.format == "gguf" && !ggufLooksValid(canonical)
                     ? "GGUF magic present but version is unsupported"
                     : "stored only; no verified inference adapter in V1";

    auto existing = std::find_if(entries_.begin(), entries_.end(),
                                 [&](const ModelEntry& item) { return item.id == entry.id; });
    if (entries_.size() >= kMaxModels && existing == entries_.end()) {
        return fail(HCS_E_SECURITY_POLICY, "models.add", "catalog is full (512 models)");
    }
    if (existing != entries_.end()) {
        *existing = entry;
    } else {
        entries_.push_back(entry);
    }
    const hcsResult result = save();
    if (result != HCS_SUCCESS) {
        return result;
    }
    *out = entry;
    return HCS_SUCCESS;
}

hcsResult ModelVault::scan()
{
    for (auto& entry : entries_) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(entry.path, error) || error) {
            entry.note = "file missing or not a regular file";
            continue;
        }
        const uint64_t size = std::filesystem::file_size(entry.path, error);
        if (!error && size != entry.size_bytes) {
            uint64_t hashed = 0;
            const std::string fresh = Sha256::hashFile(entry.path, &hashed);
            if (fresh.size() == 64 && fresh != entry.sha256) {
                entry.sha256 = fresh;
                entry.id = fresh.substr(0, 16);
                entry.size_bytes = hashed;
                entry.hash_verified = true;
                entry.note = "file changed since import; checksum refreshed";
            }
        }
    }
    return save();
}

hcsResult ModelVault::list(std::vector<ModelEntry>* out) const
{
    if (!out) {
        return fail(HCS_E_INVALID_ARGUMENT, "models.list", "output must not be null");
    }
    *out = entries_;
    return HCS_SUCCESS;
}

hcsResult ModelVault::remove(const std::string& id)
{
    const auto before = entries_.size();
    entries_.erase(std::remove_if(entries_.begin(), entries_.end(),
                                  [&](const ModelEntry& item) { return item.id == id; }),
                   entries_.end());
    if (entries_.size() == before) {
        return fail(HCS_E_INVALID_ARGUMENT, "models.remove", "unknown model id");
    }
    return save();
}

} // namespace hcs::models
