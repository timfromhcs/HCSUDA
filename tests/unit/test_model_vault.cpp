#include "test_framework.h"

#include "model_store/model_vault.h"
#include "model_store/sha256.h"

#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;

namespace {

fs::path testRoot(const char* name)
{
    const fs::path base = fs::temp_directory_path() / "hcsuda_model_tests";
    fs::create_directories(base);
    return base / name;
}

void writeFile(const fs::path& path, const std::string& bytes)
{
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
}

std::string lastMessage()
{
    hcsErrorInfo info{};
    info.struct_size = sizeof(info);
    hcsGetLastError(&info);
    return info.message;
}

std::string makeGguf(uint32_t version)
{
    std::string data = "GGUF";
    for (int i = 0; i < 4; ++i) {
        data.push_back(static_cast<char>((version >> (i * 8)) & 0xFF));
    }
    data.append(24, '\0'); /* counts/header padding for this fixture */
    return data;
}

} // namespace

HCS_TEST(sha256_matches_known_vectors)
{
    using hcs::models::Sha256;
    CHECK_EQ(Sha256().finish(),
             std::string("e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855"));
    Sha256 abc;
    abc.update("abc", 3);
    CHECK_EQ(abc.finish(),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
    Sha256 split;
    split.update("a", 1);
    split.update("bc", 2);
    CHECK_EQ(split.finish(),
             std::string("ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad"));
}

HCS_TEST(model_vault_imports_and_persists_gguf)
{
    const fs::path root = testRoot("import_persist");
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path model = root / "tinyllama-q4_k_m.gguf";
    const std::string bytes = makeGguf(3);
    writeFile(model, bytes);

    hcs::models::ModelVault vault(root);
    HCS_REQUIRE_OK(vault.initialize());
    hcs::models::ModelEntry entry;
    HCS_REQUIRE_OK(vault.add(model.string(), &entry));
    CHECK_EQ(entry.format, std::string("gguf"));
    CHECK_EQ(entry.quantization, std::string("q4_k_m"));
    CHECK_EQ(entry.size_bytes, static_cast<uint64_t>(bytes.size()));
    CHECK(entry.hash_verified);
    CHECK(!entry.runnable); /* no adapter in V1 */
    CHECK_MSG(entry.sha256.size() == 64, "SHA-256 must be lowercase hex");

    hcs::models::ModelVault reopened(root);
    HCS_REQUIRE_OK(reopened.initialize());
    std::vector<hcs::models::ModelEntry> listed;
    HCS_REQUIRE_OK(reopened.list(&listed));
    CHECK_EQ(listed.size(), std::size_t{1});
    CHECK_EQ(listed[0].id, entry.id);
    CHECK_EQ(listed[0].sha256, entry.sha256);
    CHECK_EQ(listed[0].size_bytes, entry.size_bytes);
    HCS_REQUIRE_OK(reopened.remove(entry.id));
    HCS_REQUIRE_OK(reopened.list(&listed));
    CHECK(listed.empty());
}

HCS_TEST(model_vault_rejects_unsafe_and_corrupt_inputs)
{
    const fs::path root = testRoot("safety");
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path outside = testRoot("outside.gguf");
    writeFile(outside, makeGguf(3));

    hcs::models::ModelVault vault(root);
    HCS_REQUIRE_OK(vault.initialize());
    hcs::models::ModelEntry entry;
    CHECK_EQ(vault.add(outside.string(), &entry), HCS_E_SECURITY_POLICY);
    CHECK_MSG(lastMessage().find("outside") != std::string::npos, lastMessage());
    CHECK_EQ(vault.add((root / "missing.gguf").string(), &entry), HCS_E_IO);
    CHECK_EQ(vault.add("", &entry), HCS_E_INVALID_ARGUMENT);

    const fs::path corrupt = root / "corrupt.gguf";
    writeFile(corrupt, std::string("NOT-GGUF-payload"));
    HCS_REQUIRE_OK(vault.add(corrupt.string(), &entry));
    CHECK_EQ(entry.format, std::string("unknown"));
    CHECK_EQ(entry.runnable, false);

    const fs::path badVersion = root / "bad-version.gguf";
    writeFile(badVersion, makeGguf(99));
    HCS_REQUIRE_OK(vault.add(badVersion.string(), &entry));
    CHECK_MSG(entry.note.find("unsupported") != std::string::npos, entry.note);
}

HCS_TEST(model_vault_scan_detects_changes)
{
    const fs::path root = testRoot("rescan");
    fs::remove_all(root);
    fs::create_directories(root);
    const fs::path model = root / "model-f16.gguf";
    writeFile(model, makeGguf(2));

    hcs::models::ModelVault vault(root);
    HCS_REQUIRE_OK(vault.initialize());
    hcs::models::ModelEntry entry;
    HCS_REQUIRE_OK(vault.add(model.string(), &entry));
    const std::string oldHash = entry.sha256;
    writeFile(model, makeGguf(2) + "extra bytes");
    HCS_REQUIRE_OK(vault.scan());
    std::vector<hcs::models::ModelEntry> listed;
    HCS_REQUIRE_OK(vault.list(&listed));
    CHECK_EQ(listed.size(), std::size_t{1});
    CHECK_MSG(listed[0].sha256 != oldHash, "checksum must be refreshed on change");
    CHECK_MSG(listed[0].note.find("changed") != std::string::npos, listed[0].note);
}
