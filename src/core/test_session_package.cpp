// Portable session package (report feature 11): a store-only ZIP that carries
// the compare session plus the images it references.
#include "core/session/SessionPackage.h"

#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <vector>

namespace fs = std::filesystem;
using mviewer::core::readSessionPackage;
using mviewer::core::sessionPackageCrc32;
using mviewer::core::SessionPackageEntry;
using mviewer::core::writeSessionPackage;

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)

std::vector<std::uint8_t> bytesOf(const std::string &text)
{
    return std::vector<std::uint8_t>(text.begin(), text.end());
}
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "mviewer_session_package_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    CHECK(!ec, "temp dir created");

    const fs::path packagePath = root / "review.mviewer-session";
    const std::string path = packagePath.string();

    std::vector<SessionPackageEntry> entries;
    entries.push_back({"session.json", bytesOf("{\"images\":[\"a.png\",\"b.png\"]}")});
    entries.push_back({"images/a.png", bytesOf("not a real png, but deterministic bytes")});
    entries.push_back({"images/b.png", bytesOf(std::string(5000, 'x'))});

    std::string error;
    CHECK(writeSessionPackage(path, entries, error) && error.empty(), "package written");
    CHECK(fs::exists(packagePath, ec), "package file exists on disk");

    const auto readBack = readSessionPackage(path, error);
    CHECK(error.empty(), "package read without error");
    CHECK(readBack.size() == 3, "all three entries came back");
    bool sessionOk = false;
    bool imageAOk = false;
    bool imageBOk = false;
    for (const SessionPackageEntry &entry : readBack)
    {
        if (entry.name == "session.json")
            sessionOk = entry.data == entries[0].data;
        else if (entry.name == "images/a.png")
            imageAOk = entry.data == entries[1].data;
        else if (entry.name == "images/b.png")
            imageBOk = entry.data == entries[2].data;
    }
    CHECK(sessionOk, "the manifest round-trips byte for byte");
    CHECK(imageAOk && imageBOk, "both images round-trip byte for byte");

    // CRC-32 check values: "a" and the empty string.
    CHECK(sessionPackageCrc32(reinterpret_cast<const std::uint8_t *>("a"), 1) == 0xE8B7BE43u,
          "crc32(\"a\") matches the published value");
    CHECK(sessionPackageCrc32(nullptr, 0) == 0u, "crc32 of nothing is zero");

    // Truncated / foreign files are rejected with a reason, never read OOB.
    const fs::path tiny = root / "tiny.mviewer-session";
    {
        std::ofstream out(tiny, std::ios::binary | std::ios::trunc);
        out << "not a zip at all";
    }
    const auto bad = readSessionPackage(tiny.string(), error);
    CHECK(bad.empty() && !error.empty(), "a non-package is rejected with a reason");

    const auto missing = readSessionPackage((root / "absent.zip").string(), error);
    CHECK(missing.empty() && !error.empty(), "a missing package is rejected with a reason");

    fs::remove_all(root, ec);
    printf("\n==== session package test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
