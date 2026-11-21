// Portable session package (report feature 11): a store-only ZIP that carries
// the compare session plus the images it references.
#include "core/session/SessionPackage.h"

#include <array>
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

// Emit a store-only ZIP with arbitrary entry names, bypassing the writer's
// validation, so the reader's own guard can be exercised directly.
bool writeRawPackage(const std::string &path, const std::vector<SessionPackageEntry> &entries)
{
    std::vector<std::uint8_t> out;
    std::vector<std::array<std::uint32_t, 3>> central;
    auto append16 = [&out](std::uint16_t v)
    {
        out.push_back(static_cast<std::uint8_t>(v & 0xFF));
        out.push_back(static_cast<std::uint8_t>(v >> 8));
    };
    auto append32 = [&out](std::uint32_t v)
    {
        for (int i = 0; i < 4; ++i)
            out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
    };
    for (const SessionPackageEntry &entry : entries)
    {
        const std::uint32_t crc = sessionPackageCrc32(entry.data.data(), entry.data.size());
        const std::uint32_t size = static_cast<std::uint32_t>(entry.data.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());
        append32(0x04034b50u);
        append16(20);
        append16(0);
        append16(0);
        append16(0);
        append16(0);
        append32(crc);
        append32(size);
        append32(size);
        append16(static_cast<std::uint16_t>(entry.name.size()));
        append16(0);
        out.insert(out.end(), entry.name.begin(), entry.name.end());
        out.insert(out.end(), entry.data.begin(), entry.data.end());
        central.push_back({crc, size, offset});
    }
    const std::uint32_t centralStart = static_cast<std::uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const SessionPackageEntry &entry = entries[i];
        append32(0x02014b50u);
        append16(20);
        append16(20);
        append16(0);
        append16(0);
        append16(0);
        append16(0);
        append32(central[i][0]);
        append32(central[i][1]);
        append32(central[i][1]);
        append16(static_cast<std::uint16_t>(entry.name.size()));
        append16(0);
        append16(0);
        append16(0);
        append16(0);
        append32(0);
        append32(central[i][2]);
        out.insert(out.end(), entry.name.begin(), entry.name.end());
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;
    append32(0x06054b50u);
    append16(0);
    append16(0);
    append16(static_cast<std::uint16_t>(entries.size()));
    append16(static_cast<std::uint16_t>(entries.size()));
    append32(centralSize);
    append32(centralStart);
    append16(0);
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    if (!file)
        return false;
    file.write(reinterpret_cast<const char *>(out.data()),
               static_cast<std::streamsize>(out.size()));
    return file.good();
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

    // Zip Slip: an entry that escapes the extraction directory must be refused
    // on read, and never offered for writing in the first place.
    const std::vector<std::string> hostile = {"../escape.png",      "images/../../escape.png",
                                              "images\\escape.png", "C:/escape.png",
                                              "/escape.png",        "other/escape.png"};
    for (const std::string &name : hostile)
    {
        std::vector<SessionPackageEntry> bad_entries;
        bad_entries.push_back({name, bytesOf("x")});
        std::string why;
        const bool wrote = writeSessionPackage((root / "hostile.zip").string(), bad_entries, why);
        CHECK(!wrote && !why.empty(), ("writer refuses " + name).c_str());
        const fs::path hostilePath = root / "hostile.zip";
        CHECK(writeRawPackage(hostilePath.string(), bad_entries), "raw hostile package written");
        const auto read = readSessionPackage(hostilePath.string(), error);
        CHECK(read.empty() && !error.empty(), ("reader refuses " + name).c_str());
    }

    fs::remove_all(root, ec);
    printf("\n==== session package test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
