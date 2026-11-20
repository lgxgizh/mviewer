#include "core/session/SessionPackage.h"

#include "core/filesystem/Utf8Path.h"

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace mviewer::core
{
namespace
{
std::uint16_t le16(std::uint16_t v)
{
    return v;
}
std::uint32_t le32(std::uint32_t v)
{
    return v;
}

void append(std::vector<std::uint8_t> &out, std::uint16_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
}

void append(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    for (int i = 0; i < 4; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

void appendBytes(std::vector<std::uint8_t> &out, const void *data, size_t size)
{
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

bool readAt(std::istream &in, std::streamoff offset, std::vector<std::uint8_t> &out, size_t size)
{
    in.clear();
    in.seekg(offset);
    out.resize(size);
    if (size == 0)
        return true;
    in.read(reinterpret_cast<char *>(out.data()), static_cast<std::streamsize>(size));
    return static_cast<size_t>(in.gcount()) == size;
}

constexpr std::uint32_t kLocalHeaderSignature = 0x04034b50u;
constexpr std::uint32_t kCentralHeaderSignature = 0x02014b50u;
constexpr std::uint32_t kEndOfCentralDirectorySignature = 0x06054b50u;
} // namespace

std::uint32_t sessionPackageCrc32(const std::uint8_t *data, size_t size)
{
    static std::uint32_t table[256];
    static bool ready = false;
    if (!ready)
    {
        for (std::uint32_t i = 0; i < 256; ++i)
        {
            std::uint32_t c = i;
            for (int k = 0; k < 8; ++k)
                c = (c & 1u) ? (0xEDB88320u ^ (c >> 1)) : (c >> 1);
            table[i] = c;
        }
        ready = true;
    }
    std::uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool writeSessionPackage(const std::string &path, const std::vector<SessionPackageEntry> &entries,
                         std::string &error)
{
    std::vector<std::uint8_t> out;
    std::vector<std::array<std::uint32_t, 3>> central; // crc, size, local offset
    for (const SessionPackageEntry &entry : entries)
    {
        if (entry.name.empty())
        {
            error = "an entry has an empty name";
            return false;
        }
        const std::uint32_t crc = sessionPackageCrc32(entry.data.data(), entry.data.size());
        const std::uint32_t size = static_cast<std::uint32_t>(entry.data.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());

        append(out, le32(kLocalHeaderSignature));
        append(out, le16(20)); // version needed
        append(out, le16(0));  // flags
        append(out, le16(0));  // method: store
        append(out, le16(0));  // mod time
        append(out, le16(0));  // mod date
        append(out, le32(crc));
        append(out, le32(size)); // compressed = stored size
        append(out, le32(size));
        append(out, le16(static_cast<std::uint16_t>(entry.name.size())));
        append(out, le16(0)); // extra length
        appendBytes(out, entry.name.data(), entry.name.size());
        appendBytes(out, entry.data.data(), entry.data.size());

        central.push_back({crc, size, offset});
    }

    const std::uint32_t centralStart = static_cast<std::uint32_t>(out.size());
    for (size_t i = 0; i < entries.size(); ++i)
    {
        const SessionPackageEntry &entry = entries[i];
        append(out, le32(kCentralHeaderSignature));
        append(out, le16(20)); // version made by
        append(out, le16(20)); // version needed
        append(out, le16(0));  // flags
        append(out, le16(0));  // method
        append(out, le16(0));  // mod time
        append(out, le16(0));  // mod date
        append(out, le32(central[i][0]));
        append(out, le32(central[i][1]));
        append(out, le32(central[i][1]));
        append(out, le16(static_cast<std::uint16_t>(entry.name.size())));
        append(out, le16(0)); // extra
        append(out, le16(0)); // comment
        append(out, le16(0)); // disk number
        append(out, le16(0)); // internal attrs
        append(out, le32(0)); // external attrs
        append(out, le32(central[i][2]));
        appendBytes(out, entry.name.data(), entry.name.size());
    }
    const std::uint32_t centralSize = static_cast<std::uint32_t>(out.size()) - centralStart;

    append(out, le32(kEndOfCentralDirectorySignature));
    append(out, le16(0)); // disk
    append(out, le16(0)); // central directory disk
    append(out, le16(static_cast<std::uint16_t>(entries.size())));
    append(out, le16(static_cast<std::uint16_t>(entries.size())));
    append(out, le32(centralSize));
    append(out, le32(centralStart));
    append(out, le16(0)); // comment length

    std::ofstream file(mviewer::core::pathFromUtf8(path), std::ios::binary | std::ios::trunc);
    if (!file)
    {
        error = "cannot create package";
        return false;
    }
    file.write(reinterpret_cast<const char *>(out.data()),
               static_cast<std::streamsize>(out.size()));
    file.close();
    if (!file)
    {
        error = "short write";
        return false;
    }
    return true;
}

std::vector<SessionPackageEntry> readSessionPackage(const std::string &path, std::string &error)
{
    std::vector<SessionPackageEntry> entries;
    std::ifstream in(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!in)
    {
        error = "cannot open package";
        return entries;
    }
    std::vector<std::uint8_t> bytes((std::istreambuf_iterator<char>(in)),
                                    std::istreambuf_iterator<char>());
    if (bytes.size() < 22)
    {
        error = "package is truncated";
        return entries;
    }
    const auto u32At = [&](size_t at) -> std::uint32_t
    {
        if (at + 4 > bytes.size())
            return 0;
        return static_cast<std::uint32_t>(bytes[at]) |
               (static_cast<std::uint32_t>(bytes[at + 1]) << 8) |
               (static_cast<std::uint32_t>(bytes[at + 2]) << 16) |
               (static_cast<std::uint32_t>(bytes[at + 3]) << 24);
    };
    const auto u16At = [&](size_t at) -> std::uint16_t
    {
        if (at + 2 > bytes.size())
            return 0;
        return static_cast<std::uint16_t>(static_cast<std::uint16_t>(bytes[at]) |
                                          (static_cast<std::uint16_t>(bytes[at + 1]) << 8));
    };

    // Locate the end-of-central-directory record from the tail.
    size_t eocd = bytes.size() >= 22 ? bytes.size() - 22 : 0;
    bool found = false;
    for (size_t at = bytes.size() >= 22 ? bytes.size() - 22 : 0; at < bytes.size(); ++at)
    {
        if (u32At(at) == kEndOfCentralDirectorySignature)
        {
            eocd = at;
            found = true;
            break;
        }
    }
    if (!found && u32At(eocd) != kEndOfCentralDirectorySignature)
    {
        error = "not a session package (no end-of-central-directory)";
        return entries;
    }
    const std::uint16_t count = u16At(eocd + 10);
    std::uint32_t centralOffset = u32At(eocd + 16);

    for (std::uint16_t i = 0; i < count; ++i)
    {
        if (centralOffset + 46 > bytes.size() || u32At(centralOffset) != kCentralHeaderSignature)
        {
            error = "corrupt central directory";
            return entries;
        }
        const std::uint16_t method = u16At(centralOffset + 10);
        const std::uint32_t crc = u32At(centralOffset + 16);
        const std::uint32_t size = u32At(centralOffset + 24);
        const std::uint16_t nameLen = u16At(centralOffset + 28);
        const std::uint16_t extraLen = u16At(centralOffset + 30);
        const std::uint16_t commentLen = u16At(centralOffset + 32);
        const std::uint32_t localOffset = u32At(centralOffset + 42);
        if (method != 0)
        {
            error = "compressed entries are not supported";
            return entries;
        }
        if (centralOffset + 46 + nameLen > bytes.size())
        {
            error = "corrupt entry name";
            return entries;
        }
        SessionPackageEntry entry;
        entry.name.assign(reinterpret_cast<const char *>(&bytes[centralOffset + 46]), nameLen);
        if (localOffset + 30 > bytes.size() || u32At(localOffset) != kLocalHeaderSignature)
        {
            error = "corrupt local header";
            return entries;
        }
        const std::uint16_t localNameLen = u16At(localOffset + 26);
        const std::uint16_t localExtraLen = u16At(localOffset + 28);
        const size_t dataAt = localOffset + 30 + localNameLen + localExtraLen;
        if (dataAt + size > bytes.size())
        {
            error = "truncated entry data";
            return entries;
        }
        entry.data.assign(bytes.begin() + dataAt, bytes.begin() + dataAt + size);
        if (sessionPackageCrc32(entry.data.data(), entry.data.size()) != crc)
        {
            error = "crc mismatch for " + entry.name;
            return entries;
        }
        entries.push_back(std::move(entry));
        centralOffset += 46 + nameLen + extraLen + commentLen;
    }
    return entries;
}

} // namespace mviewer::core
