// Portable session package (report feature 11): a store-only ZIP that carries
// the compare session plus the images it references.
#include "core/session/SessionPackage.h"
#include "core/session/SessionPackageInternal.h"

#include <atomic>
#include <sstream>
#include <thread>

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

void concurrentCrcAndPackages(const fs::path &root)
{
    std::atomic<bool> start{false};
    std::atomic<bool> good{true};
    std::vector<std::thread> threads;
    for (int i = 0; i < 8; ++i)
    {
        threads.emplace_back(
            [&, i]
            {
                while (!start.load(std::memory_order_acquire))
                    std::this_thread::yield();
                const std::string path = (root / ("thread-" + std::to_string(i) + ".zip")).string();
                for (int repeat = 0; repeat < 20; ++repeat)
                {
                    const auto *text = reinterpret_cast<const std::uint8_t *>("123456789");
                    if (sessionPackageCrc32(text, 9) != 0xcbf43926u)
                        good = false;
                    const std::vector<SessionPackageEntry> entries{{"session.json", bytesOf("{}")}};
                    std::string error;
                    if (!writeSessionPackage(path, entries, error))
                        good = false;
                    const auto read = readSessionPackage(path, error);
                    if (!error.empty() || read.size() != 1 || read[0].data != entries[0].data)
                        good = false;
                }
            });
    }
    start.store(true, std::memory_order_release);
    for (auto &thread : threads)
        thread.join();
    CHECK(good.load(), "concurrent first CRC use / package roundtrips and known vector");
}

void checkBoundedReads(const fs::path &root)
{
    namespace detail = mviewer::core::session_detail;
    std::vector<std::uint8_t> bytes;
    std::string error;
    std::istringstream cap("abc");
    CHECK(!detail::readSizedStream(cap, detail::kPackageLimit + 1, bytes, error) && bytes.empty(),
          "oversize rejected before allocation/read");
    std::istringstream shortStream("ab");
    CHECK(!detail::readSizedStream(shortStream, 3, bytes, error) && bytes.empty(),
          "shrink detected without partial input");
    std::istringstream growing("abcd");
    CHECK(!detail::readSizedStream(growing, 3, bytes, error) && bytes.empty(),
          "growth detected with one bounded extra byte");
    const auto path = root / "oversize.zip";
    {
        std::ofstream file(path, std::ios::binary);
        file.put('x');
    }
    std::error_code ec;
    fs::resize_file(path, detail::kPackageLimit + 1, ec);
    CHECK(!ec, "sparse oversized package fixture sized");
    CHECK(readSessionPackage(path.string(), error).empty() && !error.empty(),
          "file size checked on opened stream before payload allocation");
    fs::remove(path, ec);
}

std::vector<std::uint8_t> loadBytes(const fs::path &path)
{
    std::ifstream input(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void patch16(std::vector<std::uint8_t> &bytes, size_t at, std::uint16_t value)
{
    bytes.at(at) = static_cast<std::uint8_t>(value);
    bytes.at(at + 1) = static_cast<std::uint8_t>(value >> 8);
}

void patch32(std::vector<std::uint8_t> &bytes, size_t at, std::uint32_t value)
{
    patch16(bytes, at, static_cast<std::uint16_t>(value));
    patch16(bytes, at + 2, static_cast<std::uint16_t>(value >> 16));
}

void rejectedBytes(const fs::path &path, const std::vector<std::uint8_t> &bytes, const char *label)
{
    {
        std::ofstream output(path, std::ios::binary | std::ios::trunc);
        if (!bytes.empty())
            output.write(reinterpret_cast<const char *>(bytes.data()), bytes.size());
        CHECK(output.good(), "malformed fixture written");
    }
    std::string error;
    const auto entries = readSessionPackage(path.string(), error);
    CHECK(entries.empty() && !error.empty(), label);
}

void checkNames(const fs::path &root)
{
    const auto path = root / "names.zip";
    const std::vector<std::string> names{"images/a:stream",
                                         "images/CON.png",
                                         "images/aux",
                                         "images/Lpt9.txt",
                                         "images/COM\xc2\xb9.png",
                                         "images/con .jpg",
                                         "images/a.",
                                         "images/a ",
                                         "images/a\x01.png",
                                         std::string("images/a\0b", 10),
                                         "images/\xff",
                                         "images/a?b"};
    for (const auto &name : names)
    {
        const std::vector<SessionPackageEntry> entries{{name, bytesOf("x")}};
        std::string error;
        CHECK(!writeSessionPackage(path.string(), entries, error), "writer refuses unsafe names");
        CHECK(writeRawPackage(path.string(), entries), "raw unsafe name fixture written");
        CHECK(readSessionPackage(path.string(), error).empty() && !error.empty(),
              "reader independently refuses unsafe names");
    }
    const std::vector<std::pair<std::string, std::string>> collisions{
        {"session.json", "session.json"},
        {"images/A.png", "images/a.png"},
        {"images/\xc4\xb1.png", "images/I.png"},
        {"images/\xc3\xa9.png", "images/e\xcc\x81.png"},
        {"images/stra\xc3\x9f"
         "e.png",
         "images/STRASSE.png"}};
    for (const auto &[a, b] : collisions)
    {
        const std::vector<SessionPackageEntry> entries{{a, bytesOf("a")}, {b, bytesOf("b")}};
        std::string error;
        CHECK(!writeSessionPackage(path.string(), entries, error), "writer refuses name collision");
        CHECK(writeRawPackage(path.string(), entries), "raw collision fixture written");
        CHECK(readSessionPackage(path.string(), error).empty() && !error.empty(),
              "name collision rejected without partial entries");
    }
    const std::vector<SessionPackageEntry> unicode{
        {"session.json", {}},
        {"images/\xe5\x9b\xbe\xe7\x89\x87.png", bytesOf("x")},
        {"images/caf\xc3\xa9.png", {}}};
    std::string error = "stale prior error";
    CHECK(writeSessionPackage(path.string(), unicode, error) && error.empty(),
          "Unicode names / empty payload writer and stale error reset");
    const auto read = readSessionPackage(path.string(), error);
    CHECK(error.empty() && read.size() == unicode.size() && read[1].name == unicode[1].name,
          "Unicode UTF-8 names roundtrip without renaming");
    CHECK(writeRawPackage(path.string(), unicode), "legacy flags-zero UTF-8 fixture written");
    CHECK(readSessionPackage(path.string(), error).size() == unicode.size() && error.empty(),
          "legacy writer flags-zero Unicode remains supported");
}

void checkZipBounds(const fs::path &root)
{
    namespace detail = mviewer::core::session_detail;
    const auto path = root / "bounds.zip";
    CHECK(writeRawPackage(path.string(),
                          {{"session.json", bytesOf("{}")}, {"images/a", bytesOf("payload")}}),
          "bounds fixture written");
    const auto original = loadBytes(path);
    const size_t end = original.size() - 22;
    const size_t central = detail::u32(original, end + 16);
    for (const size_t field : {end + 12, end + 16, central + 24, central + 42})
    {
        auto changed = original;
        patch32(changed, field, 0xfffffff0u);
        rejectedBytes(path, changed, "extreme central/local/size offset rejected");
    }
    for (const size_t field : {central + 28, central + 30, central + 32, size_t(26), size_t(28)})
    {
        auto changed = original;
        patch16(changed, field, 65535);
        rejectedBytes(path, changed, "truncated variable metadata rejected");
    }
    for (size_t size = 0; size < original.size(); ++size)
        rejectedBytes(path, {original.begin(), original.begin() + size},
                      "every truncation rejected");
    auto changed = original;
    patch16(changed, 6, 8);
    rejectedBytes(path, changed, "local/central flags mismatch rejected");
    changed = original;
    changed[30] ^= 1;
    rejectedBytes(path, changed, "local/central name mismatch rejected");
    changed = original;
    patch32(changed, central + 42, static_cast<std::uint32_t>(central));
    rejectedBytes(path, changed, "local header in central directory rejected");
    changed = original;
    patch32(changed, central + 38, 0120777u << 16);
    rejectedBytes(path, changed, "Unix symlink entry metadata rejected");
    changed = original;
    changed[central - 1] ^= 1;
    rejectedBytes(path, changed, "late CRC mismatch returns no earlier entries");
    changed = original;
    patch16(changed, end + 20, 3);
    changed.insert(changed.end(), {'a', 'b', 'c'});
    {
        std::ofstream file(path, std::ios::binary | std::ios::trunc);
        file.write(reinterpret_cast<const char *>(changed.data()), changed.size());
    }
    std::string error;
    CHECK(readSessionPackage(path.string(), error).size() == 2 && error.empty(),
          "valid EOCD comment is found backwards");
}

void checkOverlappingEntries(const fs::path &root)
{
    namespace detail = mviewer::core::session_detail;
    const auto path = root / "overlap.zip";
    const SessionPackageEntry inner{"images/inner", bytesOf("inner")};
    CHECK(writeRawPackage(path.string(), {inner}), "inner local record fixture written");
    auto innerZip = loadBytes(path);
    innerZip.resize(30 + inner.name.size() + inner.data.size());
    const SessionPackageEntry outer{"images/outer", innerZip};
    CHECK(writeRawPackage(path.string(), {outer, inner}), "nested local record fixture written");
    auto bytes = loadBytes(path);
    const size_t central = detail::u32(bytes, bytes.size() - 6);
    const size_t second = central + 46 + outer.name.size();
    patch32(bytes, second + 42, static_cast<std::uint32_t>(30 + outer.name.size()));
    rejectedBytes(path, bytes, "fully valid nested local header rejected as overlapping");
}

} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "mviewer_session_package_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    CHECK(!ec, "temp dir created");
    // First CRC use in this process is concurrent, before fixture creation.
    concurrentCrcAndPackages(root);
    checkBoundedReads(root);
    checkNames(root);
    checkZipBounds(root);
    checkOverlappingEntries(root);

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
