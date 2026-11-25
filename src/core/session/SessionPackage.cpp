#include "core/session/SessionPackage.h"

#include "core/filesystem/Utf8Path.h"
#include "core/session/SessionPackageInternal.h"

#include <QByteArray>
#include <QString>

#include <array>
#include <cstdio>
#include <cstring>
#include <fstream>

namespace mviewer::core
{
namespace
{
using namespace session_detail;

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
    if (size == 0)
        return;
    const auto *bytes = static_cast<const std::uint8_t *>(data);
    out.insert(out.end(), bytes, bytes + size);
}

bool reservedDevice(QString stem)
{
    stem = stem.section(QLatin1Char('.'), 0, 0).trimmed().toCaseFolded();
    for (const auto *name : {"con", "prn", "aux", "nul", "conin$", "conout$", "clock$"})
        if (stem == QLatin1String(name))
            return true;
    if (stem.size() != 4 ||
        (!stem.startsWith(QLatin1String("com")) && !stem.startsWith(QLatin1String("lpt"))))
        return false;
    const auto digit = stem.at(3).unicode();
    return (digit >= '1' && digit <= '9') || digit == 0x00b9 || digit == 0x00b2 || digit == 0x00b3;
}

bool entryKey(const std::string &name, std::string &key, std::string &error)
{
    if (name.empty() || name.size() > 65535)
        return fail(error, "entry name is empty or exceeds classic ZIP limits");
    const QString text = QString::fromUtf8(name.data(), static_cast<qsizetype>(name.size()));
    if (text.toUtf8().toStdString() != name)
        return fail(error, "entry name is not valid UTF-8");
    for (const auto ch : text)
        if (ch.category() == QChar::Other_Control || QStringLiteral("\\:*?\"<>|").contains(ch))
            return fail(error, "entry name contains control or non-portable characters");
    if (name != "session.json")
    {
        if (!text.startsWith(QLatin1String("images/")))
            return fail(error, "entry outside session.json / images/");
        const QString leaf = text.mid(7);
        if (leaf.isEmpty() || leaf.contains(QLatin1Char('/')) || leaf == QLatin1String(".") ||
            leaf == QLatin1String("..") || leaf.endsWith(QLatin1Char('.')) ||
            leaf.endsWith(QLatin1Char(' ')) || reservedDevice(leaf))
            return fail(error, "image entry is not a portable plain file name");
    }
    // Qt uses simple folding. Uppercase expansion also conservatively catches
    // sharp-s/SS and dotless-I/I portable filesystem collisions; keep names intact.
    key = text.normalized(QString::NormalizationForm_C)
              .toCaseFolded()
              .toUpper()
              .toCaseFolded()
              .normalized(QString::NormalizationForm_C)
              .toUtf8()
              .toStdString();
    return true;
}

bool uniqueEntry(const std::string &name, std::set<std::string> &names, std::string &error)
{
    std::string key;
    if (!entryKey(name, key, error))
        return false;
    if (!names.insert(key).second)
        return fail(error, "duplicate or normalized/case-folded entry name");
    return true;
}

bool validateWrite(const std::vector<SessionPackageEntry> &entries, std::string &error)
{
    if (entries.size() >= 65535)
        return fail(error, "too many entries for a classic ZIP");
    size_t payload = 0;
    size_t archive = 22;
    std::set<std::string> names;
    for (const auto &entry : entries)
    {
        if (!uniqueEntry(entry.name, names, error))
            return false;
        if (entry.data.size() > kEntryLimit || entry.data.size() > kPayloadLimit - payload)
            return fail(error, "entry or aggregate payload exceeds the size limit");
        payload += entry.data.size();
        const size_t needed = 76 + 2 * entry.name.size() + entry.data.size();
        if (needed > kPackageLimit - archive)
            return fail(error, "package exceeds the bounded input limit");
        archive += needed;
    }
    return true;
}
} // namespace

std::uint32_t sessionPackageCrc32(const std::uint8_t *data, size_t size)
{
    static constexpr auto table = []
    {
        std::array<std::uint32_t, 256> result{};
        for (std::uint32_t i = 0; i < result.size(); ++i)
        {
            auto value = i;
            for (int bit = 0; bit < 8; ++bit)
                value = (value & 1u) ? (0xEDB88320u ^ (value >> 1)) : (value >> 1);
            result[i] = value;
        }
        return result;
    }();
    std::uint32_t crc = 0xFFFFFFFFu;
    for (size_t i = 0; i < size; ++i)
        crc = table[(crc ^ data[i]) & 0xFFu] ^ (crc >> 8);
    return crc ^ 0xFFFFFFFFu;
}

bool writeSessionPackage(const std::string &path, const std::vector<SessionPackageEntry> &entries,
                         std::string &error)
{
    error.clear();
    if (!validateWrite(entries, error))
        return false;
    std::vector<std::uint8_t> out;
    std::vector<std::array<std::uint32_t, 3>> central; // crc, size, local offset
    for (const SessionPackageEntry &entry : entries)
    {
        const std::uint32_t crc = sessionPackageCrc32(entry.data.data(), entry.data.size());
        const std::uint32_t size = static_cast<std::uint32_t>(entry.data.size());
        const std::uint32_t offset = static_cast<std::uint32_t>(out.size());

        append(out, le32(kLocal));
        append(out, le16(20));     // version needed
        append(out, le16(0x0800)); // UTF-8 names
        append(out, le16(0));      // method: store
        append(out, le16(0));      // mod time
        append(out, le16(0));      // mod date
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
        append(out, le32(kCentral));
        append(out, le16(20));     // version made by
        append(out, le16(20));     // version needed
        append(out, le16(0x0800)); // UTF-8 names
        append(out, le16(0));      // method
        append(out, le16(0));      // mod time
        append(out, le16(0));      // mod date
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

    append(out, le32(kEnd));
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
    error.clear();
    std::ifstream in(pathFromUtf8(path), std::ios::binary | std::ios::ate);
    if (!in)
    {
        error = "cannot open package";
        return {};
    }
    const auto length = in.tellg();
    if (length < 0 || static_cast<std::uint64_t>(length) > kPackageLimit)
    {
        error = "package exceeds bounded input limit or is not seekable";
        return {};
    }
    in.seekg(0);
    std::vector<std::uint8_t> bytes;
    if (!readSizedStream(in, static_cast<size_t>(length), bytes, error))
        return {};
    Directory dir;
    if (!locateDirectory(bytes, dir, error))
        return {};
    std::vector<SessionPackageEntry> entries;
    std::set<std::string> names;
    std::map<size_t, size_t> spans;
    size_t at = dir.start;
    size_t total = 0;
    for (size_t i = 0; i < dir.count; ++i)
    {
        Record record;
        if (!readCentral(bytes, at, dir.end, record, error) ||
            !uniqueEntry(record.name, names, error))
            return {};
        if (record.size > kEntryLimit || record.size > kPayloadLimit - total)
        {
            error = "entry or aggregate payload exceeds the size limit";
            return {};
        }
        size_t dataAt = 0;
        if (!localPayload(bytes, record, dir.start, dataAt, error) ||
            !registerSpan(spans, record.local, dataAt + record.size, error))
            return {};
        if (sessionPackageCrc32(bytes.data() + dataAt, record.size) != record.crc)
        {
            error = "crc mismatch for " + record.name;
            return {};
        }
        entries.push_back(
            {record.name, {bytes.begin() + dataAt, bytes.begin() + dataAt + record.size}});
        total += record.size;
        at = record.next;
    }
    if (at != dir.end)
    {
        error = "central directory count/size mismatch";
        return {};
    }
    return entries;
}

} // namespace mviewer::core
