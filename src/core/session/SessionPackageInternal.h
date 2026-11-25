#pragma once

// Internal classic/store-only ZIP helpers. Not a public extraction API.
#include "core/session/SessionPackage.h"

#include <algorithm>
#include <array>
#include <istream>
#include <iterator>
#include <map>
#include <set>
#include <string_view>

namespace mviewer::core::session_detail
{
constexpr std::uint32_t kLocal = 0x04034b50u;
constexpr std::uint32_t kCentral = 0x02014b50u;
constexpr std::uint32_t kEnd = 0x06054b50u;
constexpr size_t kEntryLimit = size_t{128} * 1024 * 1024;
constexpr size_t kPayloadLimit = size_t{256} * 1024 * 1024;
// Bound metadata too; keep the existing 256 MiB aggregate payload allowance.
constexpr size_t kPackageLimit = kPayloadLimit + size_t{16} * 1024 * 1024;

inline bool inside(size_t limit, size_t at, size_t size)
{
    return at <= limit && size <= limit - at;
}

// Callers first validate the complete fixed header span with inside().
inline std::uint16_t u16(const std::vector<std::uint8_t> &bytes, size_t at)
{
    return static_cast<std::uint16_t>(bytes[at] | (std::uint16_t(bytes[at + 1]) << 8));
}
inline std::uint32_t u32(const std::vector<std::uint8_t> &bytes, size_t at)
{
    return std::uint32_t(u16(bytes, at)) | (std::uint32_t(u16(bytes, at + 2)) << 16);
}

inline bool fail(std::string &error, const char *why)
{
    error = why;
    return false;
}

// Separate from seek/tell so short/growing streams can be tested
// deterministically.
inline bool readSizedStream(std::istream &in, size_t size, std::vector<std::uint8_t> &bytes,
                            std::string &error)
{
    bytes.clear();
    if (size > kPackageLimit)
        return fail(error, "package exceeds the bounded input limit");
    bytes.resize(size);
    if (size != 0)
        in.read(reinterpret_cast<char *>(bytes.data()), static_cast<std::streamsize>(size));
    if (!in || (size != 0 && static_cast<size_t>(in.gcount()) != size))
    {
        bytes.clear();
        return fail(error, "package shrank or could not be read");
    }
    char extra = 0;
    if (in.get(extra) || !in.eof())
    {
        bytes.clear();
        return fail(error, "package grew or could not be read");
    }
    return true;
}

struct Directory
{
    size_t start = 0;
    size_t end = 0;
    std::uint16_t count = 0;
};

inline bool locateDirectory(const std::vector<std::uint8_t> &bytes, Directory &dir,
                            std::string &error)
{
    if (bytes.size() < 22)
        return fail(error, "package is truncated");
    const size_t first = bytes.size() > 22u + 65535u ? bytes.size() - 22u - 65535u : 0;
    for (size_t at = bytes.size() - 22;; --at)
    {
        if (u32(bytes, at) == kEnd && u16(bytes, at + 20) == bytes.size() - at - 22)
        {
            if (u16(bytes, at + 4) != 0 || u16(bytes, at + 6) != 0 ||
                u16(bytes, at + 8) != u16(bytes, at + 10) || u16(bytes, at + 10) == 65535)
                return fail(error, "split/ZIP64 packages are unsupported");
            dir.start = u32(bytes, at + 16);
            dir.end = at;
            dir.count = u16(bytes, at + 10);
            const size_t size = u32(bytes, at + 12);
            if (!inside(at, dir.start, size) || dir.start + size != at)
                return fail(error, "corrupt central directory span");
            return true;
        }
        if (at == first)
            break;
    }
    return fail(error, "not a session package (no end-of-central-directory)");
}

struct Record
{
    std::string name;
    std::uint16_t flags = 0;
    std::uint32_t crc = 0;
    size_t size = 0;
    size_t local = 0;
    size_t next = 0;
};

inline bool readCentral(const std::vector<std::uint8_t> &bytes, size_t at, size_t end,
                        Record &record, std::string &error)
{
    if (!inside(end, at, 46) || u32(bytes, at) != kCentral)
        return fail(error, "corrupt central directory record");
    record.flags = u16(bytes, at + 8);
    record.crc = u32(bytes, at + 16);
    record.size = u32(bytes, at + 24);
    record.local = u32(bytes, at + 42);
    const auto mode = (u32(bytes, at + 38) >> 16) & 0170000u;
    if (u16(bytes, at + 6) > 20 || (record.flags & ~0x0800u) != 0 || u16(bytes, at + 10) != 0 ||
        u32(bytes, at + 20) != record.size || u16(bytes, at + 34) != 0 ||
        (mode != 0 && mode != 0100000u) || (u32(bytes, at + 38) & 0x10u) != 0)
        return fail(error, "unsupported entry flags, method, disk, or file type");
    const size_t nameSize = u16(bytes, at + 28);
    const size_t variable = nameSize + size_t(u16(bytes, at + 30)) + u16(bytes, at + 32);
    if (!inside(end, at + 46, variable))
        return fail(error, "truncated central entry metadata");
    record.name.assign(reinterpret_cast<const char *>(bytes.data() + at + 46), nameSize);
    record.next = at + 46 + variable;
    return true;
}

inline bool localPayload(const std::vector<std::uint8_t> &bytes, const Record &record,
                         size_t centralStart, size_t &dataAt, std::string &error)
{
    const size_t at = record.local;
    if (!inside(centralStart, at, 30) || u32(bytes, at) != kLocal)
        return fail(error, "corrupt local header");
    if (u16(bytes, at + 4) > 20 || u16(bytes, at + 6) != record.flags || u16(bytes, at + 8) != 0 ||
        u32(bytes, at + 14) != record.crc || u32(bytes, at + 18) != record.size ||
        u32(bytes, at + 22) != record.size)
        return fail(error, "local and central headers disagree");
    const size_t nameSize = u16(bytes, at + 26);
    const size_t metadata = nameSize + size_t(u16(bytes, at + 28));
    if (!inside(centralStart, at + 30, metadata) || nameSize != record.name.size())
        return fail(error, "corrupt local entry metadata");
    const std::string_view name(reinterpret_cast<const char *>(bytes.data() + at + 30), nameSize);
    if (name != record.name)
        return fail(error, "local and central names disagree");
    dataAt = at + 30 + metadata;
    if (!inside(centralStart, dataAt, record.size))
        return fail(error, "truncated entry or overlap with central directory");
    return true;
}

inline bool registerSpan(std::map<size_t, size_t> &spans, size_t begin, size_t end,
                         std::string &error)
{
    const auto next = spans.lower_bound(begin);
    if (next != spans.end() && next->first < end)
        return fail(error, "overlapping local entries");
    if (next != spans.begin() && std::prev(next)->second > begin)
        return fail(error, "overlapping local entries");
    spans.emplace(begin, end);
    return true;
}
} // namespace mviewer::core::session_detail
