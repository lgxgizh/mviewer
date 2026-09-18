#include "core/image/RawMetadata.h"
#include "core/filesystem/Utf8Path.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <vector>

namespace mviewer::core
{

namespace
{

static std::string toLower(const std::string &s)
{
    std::string r = s;
    std::transform(r.begin(), r.end(), r.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return r;
}

static bool isRawExtension(const std::string &path)
{
    const std::filesystem::path p = pathFromUtf8(path);
    std::string ext = toLower(pathToUtf8(p.extension()));
    return ext == ".dng" || ext == ".arw" || ext == ".nef" || ext == ".orf" || ext == ".rw2" ||
           ext == ".raf" || ext == ".cr2" || ext == ".cr3" || ext == ".crw" || ext == ".pef" ||
           ext == ".srw" || ext == ".x3f" || ext == ".erf" || ext == ".mos" || ext == ".raw";
}

// Subtraction form prevents 32-bit/64-bit integer overflow when checking buffer bounds.
static bool fits(uint64_t offset, uint64_t need, uint64_t size)
{
    return offset <= size && need <= size - offset;
}

struct TiffHeader
{
    bool littleEndian = false;
    uint32_t ifdOffset = 0;
};

static bool readTiffHeader(FILE *f, uint64_t fileSize, TiffHeader &h)
{
    if (fileSize < 8)
        return false;
    uint8_t buf[8];
    if (std::fread(buf, 1, 8, f) != 8)
        return false;
    if (buf[0] == 'I' && buf[1] == 'I')
        h.littleEndian = true;
    else if (buf[0] == 'M' && buf[1] == 'M')
        h.littleEndian = false;
    else
        return false;
    uint16_t magic = h.littleEndian ? static_cast<uint16_t>(buf[2] | (buf[3] << 8))
                                    : static_cast<uint16_t>((buf[2] << 8) | buf[3]);
    if (magic != 42)
        return false;
    h.ifdOffset =
        h.littleEndian
            ? static_cast<uint32_t>(buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24))
            : static_cast<uint32_t>((buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7]);
    return fits(h.ifdOffset, 2, fileSize);
}

static uint16_t readU16(FILE *f, bool le)
{
    uint8_t b[2];
    if (std::fread(b, 1, 2, f) != 2)
        return 0;
    return le ? static_cast<uint16_t>(b[0] | (b[1] << 8))
              : static_cast<uint16_t>((b[0] << 8) | b[1]);
}

static uint32_t readU32(FILE *f, bool le)
{
    uint8_t b[4];
    if (std::fread(b, 1, 4, f) != 4)
        return 0;
    return le ? static_cast<uint32_t>(b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24))
              : static_cast<uint32_t>((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

struct IfdEntry
{
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    uint32_t value = 0;
};

static bool readEntry(FILE *f, bool le, IfdEntry &e)
{
    uint8_t b[12];
    if (std::fread(b, 1, 12, f) != 12)
        return false;
    if (le)
    {
        e.tag = static_cast<uint16_t>(b[0] | (b[1] << 8));
        e.type = static_cast<uint16_t>(b[2] | (b[3] << 8));
        e.count = static_cast<uint32_t>(b[4] | (b[5] << 8) | (b[6] << 16) | (b[7] << 24));
        e.value = static_cast<uint32_t>(b[8] | (b[9] << 8) | (b[10] << 16) | (b[11] << 24));
    }
    else
    {
        e.tag = static_cast<uint16_t>((b[0] << 8) | b[1]);
        e.type = static_cast<uint16_t>((b[2] << 8) | b[3]);
        e.count = static_cast<uint32_t>((b[4] << 24) | (b[5] << 16) | (b[6] << 8) | b[7]);
        e.value = static_cast<uint32_t>((b[8] << 24) | (b[9] << 16) | (b[10] << 8) | b[11]);
    }
    return true;
}

static std::string readString(FILE *f, uint64_t fileSize, uint32_t offset, uint32_t count)
{
    if (count == 0 || count > 4096 || !fits(offset, count, fileSize))
        return "";
    const long cur = std::ftell(f);
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0)
        return "";
    std::string s(count, '\0');
    const size_t readCount = std::fread(s.data(), 1, count, f);
    std::fseek(f, cur, SEEK_SET);
    if (readCount != count)
        return "";
    while (!s.empty() && s.back() == '\0')
        s.pop_back();
    return s;
}

static uint16_t readTagU16(FILE *f, bool le, uint64_t fileSize, uint32_t v, uint16_t type,
                           uint32_t count)
{
    if (type == 3 && count <= 2)
        return le ? static_cast<uint16_t>(v & 0xFFFF) : static_cast<uint16_t>((v >> 16) & 0xFFFF);
    if (!fits(v, 2, fileSize))
        return 0;
    const long cur = std::ftell(f);
    if (std::fseek(f, static_cast<long>(v), SEEK_SET) != 0)
        return 0;
    const uint16_t r = readU16(f, le);
    std::fseek(f, cur, SEEK_SET);
    return r;
}

static uint32_t readTagU32(FILE *f, bool le, uint64_t fileSize, uint32_t v, uint16_t type,
                           uint32_t count)
{
    if (type == 4 && count == 1)
        return v;
    if (!fits(v, 4, fileSize))
        return 0;
    const long cur = std::ftell(f);
    if (std::fseek(f, static_cast<long>(v), SEEK_SET) != 0)
        return 0;
    const uint32_t r = readU32(f, le);
    std::fseek(f, cur, SEEK_SET);
    return r;
}

static double readRational(FILE *f, bool le, uint64_t fileSize, uint32_t offset)
{
    if (!fits(offset, 8, fileSize))
        return 0.0;
    const long cur = std::ftell(f);
    if (std::fseek(f, static_cast<long>(offset), SEEK_SET) != 0)
        return 0.0;
    const uint32_t num = readU32(f, le);
    const uint32_t den = readU32(f, le);
    std::fseek(f, cur, SEEK_SET);
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

static bool parseCameraIdentityTag(FILE *f, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x010F:
        if (e.type == 2 && e.count > 0 && rm.make.empty())
            rm.make = readString(f, fileSize, e.value, e.count);
        return true;
    case 0x0110:
        if (e.type == 2 && e.count > 0 && rm.model.empty())
            rm.model = readString(f, fileSize, e.value, e.count);
        return true;
    default:
        return false;
    }
}

static bool parseDescriptionDateTag(FILE *f, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x010E:
        if (e.type == 2 && e.count > 0 && rm.description.empty())
            rm.description = readString(f, fileSize, e.value, e.count);
        return true;
    case 0x0132:
        if (e.type == 2 && e.count > 0 && rm.dateTime.empty())
            rm.dateTime = readString(f, fileSize, e.value, e.count);
        return true;
    case 0x9003:
        if (e.type == 2 && e.count > 0 && rm.dateTimeOriginal.empty())
            rm.dateTimeOriginal = readString(f, fileSize, e.value, e.count);
        return true;
    case 0x9286:
        if (e.type == 7 && e.count > 0 && rm.description.empty())
            rm.description = readString(f, fileSize, e.value, e.count);
        return true;
    default:
        return false;
    }
}

static bool parseLensIdentityTag(FILE *f, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0xA431:
        if (e.type == 2 && e.count > 0 && rm.serialNumber.empty())
            rm.serialNumber = readString(f, fileSize, e.value, e.count);
        return true;
    case 0xA433:
        if (e.type == 2 && e.count > 0 && rm.lensMaker.empty())
            rm.lensMaker = readString(f, fileSize, e.value, e.count);
        return true;
    case 0xA434:
        if (e.type == 2 && e.count > 0 && rm.lens.empty())
            rm.lens = readString(f, fileSize, e.value, e.count);
        return true;
    case 0xA435:
        if (e.type == 2 && e.count > 0 && rm.serialNumber.empty())
            rm.serialNumber = readString(f, fileSize, e.value, e.count);
        return true;
    default:
        return false;
    }
}

static bool parseIdentityTag(FILE *f, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    return parseCameraIdentityTag(f, fileSize, e, rm) ||
           parseDescriptionDateTag(f, fileSize, e, rm) || parseLensIdentityTag(f, fileSize, e, rm);
}

static bool parseExposureRationalTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e,
                                     RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x829A:
        if (e.type == 5 && e.count == 1 && rm.exposureSec == 0.0)
            rm.exposureSec = readRational(f, le, fileSize, e.value);
        return true;
    case 0x829D:
        if (e.type == 5 && e.count == 1 && rm.fNumber == 0.0)
            rm.fNumber = readRational(f, le, fileSize, e.value);
        return true;
    case 0x9201:
        if (e.type == 5 && e.count == 1 && rm.shutterSpeedValue == 0.0)
        {
            const double sv = readRational(f, le, fileSize, e.value);
            if (sv > 0.0)
                rm.shutterSpeedValue = sv;
        }
        return true;
    case 0x9202:
        if (e.type == 5 && e.count == 1 && rm.apertureValue == 0.0)
            rm.apertureValue = readRational(f, le, fileSize, e.value);
        return true;
    case 0x9203:
        if (e.type == 5 && e.count == 1 && rm.brightnessValue == 0.0)
            rm.brightnessValue = readRational(f, le, fileSize, e.value);
        return true;
    case 0x9204:
        if (e.type == 5 && e.count == 1 && rm.exposureCompensation == 0.0)
            rm.exposureCompensation = readRational(f, le, fileSize, e.value);
        return true;
    default:
        return false;
    }
}

static bool parseExposureAuxTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e,
                                RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x8827:
        if (e.type == 3 && e.count >= 1 && rm.iso == 0)
        {
            rm.isoSpeed = readTagU16(f, le, fileSize, e.value, e.type, e.count);
            rm.iso = rm.isoSpeed;
        }
        return true;
    case 0x920A:
        if (e.type == 5 && e.count == 1 && rm.focalLength == 0.0)
            rm.focalLength = readRational(f, le, fileSize, e.value);
        return true;
    case 0xA405:
        if (e.type == 3 && e.count == 1 && rm.focalLength35mm == 0.0)
            rm.focalLength35mm =
                static_cast<double>(readTagU16(f, le, fileSize, e.value, e.type, e.count));
        return true;
    default:
        return false;
    }
}

static bool parseExposureTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e,
                             RawMetadata &rm)
{
    return parseExposureRationalTag(f, le, fileSize, e, rm) ||
           parseExposureAuxTag(f, le, fileSize, e, rm);
}

static bool parseModeTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x9207:
        if (e.type == 3 && e.count == 1 && rm.meteringMode.empty())
        {
            switch (readTagU16(f, le, fileSize, e.value, e.type, e.count))
            {
            case 1:
                rm.meteringMode = "Average";
                break;
            case 2:
                rm.meteringMode = "Center-weighted average";
                break;
            case 3:
                rm.meteringMode = "Spot";
                break;
            case 4:
                rm.meteringMode = "Multi-spot";
                break;
            case 5:
                rm.meteringMode = "Multi-segment";
                break;
            case 6:
                rm.meteringMode = "Partial";
                break;
            case 255:
                rm.meteringMode = "Other";
                break;
            default:
                rm.meteringMode = "Unknown";
                break;
            }
        }
        return true;
    case 0x9208:
        if (e.type == 3 && e.count == 1 && rm.flash.empty())
        {
            const uint16_t value = readTagU16(f, le, fileSize, e.value, e.type, e.count);
            rm.flash = (value & 1) ? "Fired" : "Did not fire";
        }
        return true;
    case 0xA217:
        if (e.type == 3 && e.count == 1 && rm.sensorType.empty())
        {
            const uint16_t value = readTagU16(f, le, fileSize, e.value, e.type, e.count);
            if (value == 2)
                rm.sensorType = "Color Filter Array";
            else if (value == 3)
                rm.sensorType = "Color Filter Array (single-chip)";
            else if (value == 1)
                rm.sensorType = "Not defined";
            else
                rm.sensorType = "Other";
        }
        return true;
    case 0xA402:
        if (e.type == 3 && e.count == 1 && rm.exposureProgram.empty())
        {
            switch (readTagU16(f, le, fileSize, e.value, e.type, e.count))
            {
            case 0:
                rm.exposureProgram = "Auto";
                break;
            case 1:
                rm.exposureProgram = "Manual";
                break;
            case 2:
                rm.exposureProgram = "Auto bracket";
                break;
            default:
                rm.exposureProgram = "Unknown";
                break;
            }
        }
        return true;
    case 0xA403:
        if (e.type == 3 && e.count == 1 && rm.whiteBalance.empty())
        {
            switch (readTagU16(f, le, fileSize, e.value, e.type, e.count))
            {
            case 0:
                rm.whiteBalance = "Auto";
                break;
            case 1:
                rm.whiteBalance = "Manual";
                break;
            default:
                rm.whiteBalance = "Unknown";
                break;
            }
        }
        return true;
    default:
        return false;
    }
}

static bool parseCalibrationTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e,
                                RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0xC614:
        if (e.type == 5 && e.count <= 12 && rm.colorMatrixCount == 0)
        {
            for (uint32_t k = 0; k < e.count && k < 12; ++k)
            {
                const uint64_t ratOffset =
                    static_cast<uint64_t>(e.value) + static_cast<uint64_t>(k) * 8;
                if (fits(ratOffset, 8, fileSize))
                    rm.colorMatrix[k] =
                        readRational(f, le, fileSize, static_cast<uint32_t>(ratOffset));
            }
            rm.colorMatrixCount = (e.count < 12u) ? e.count : 12u;
        }
        return true;
    case 0xC61A:
        if (e.type == 3 && e.count >= 1 && rm.blackLevel == 0)
            rm.blackLevel = readTagU16(f, le, fileSize, e.value, e.type, e.count);
        return true;
    case 0xC61B:
        if (e.type == 3 && e.count >= 1 && rm.whiteLevel == 0)
            rm.whiteLevel = readTagU16(f, le, fileSize, e.value, e.type, e.count);
        return true;
    case 0xC628:
        if (e.type == 5 && e.count <= 4 && rm.whiteBalanceCount == 0)
        {
            for (uint32_t k = 0; k < e.count && k < 4; ++k)
            {
                const uint64_t ratOffset =
                    static_cast<uint64_t>(e.value) + static_cast<uint64_t>(k) * 8;
                if (fits(ratOffset, 8, fileSize))
                    rm.whiteBalanceRGGB[k] =
                        readRational(f, le, fileSize, static_cast<uint32_t>(ratOffset));
            }
            rm.whiteBalanceCount = (e.count < 4u) ? e.count : 4u;
        }
        return true;
    default:
        return false;
    }
}

static bool parseSensorTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x0100: // ImageWidth
        if (rm.width == 0)
            rm.width = (e.type == 3) ? readTagU16(f, le, fileSize, e.value, e.type, e.count)
                                     : readTagU32(f, le, fileSize, e.value, e.type, e.count);
        return true;
    case 0x0101: // ImageLength / Height
        if (rm.height == 0)
            rm.height = (e.type == 3) ? readTagU16(f, le, fileSize, e.value, e.type, e.count)
                                      : readTagU32(f, le, fileSize, e.value, e.type, e.count);
        return true;
    case 0x0102: // BitsPerSample
        if (rm.bitsPerSample == 0 && e.count >= 1)
            rm.bitsPerSample = readTagU16(f, le, fileSize, e.value, e.type, e.count);
        return true;
    case 0xA001: // ColorSpace
        if (rm.colorSpace.empty() && e.count == 1)
        {
            const uint16_t cs = readTagU16(f, le, fileSize, e.value, e.type, e.count);
            if (cs == 1)
                rm.colorSpace = "sRGB";
            else if (cs == 2)
                rm.colorSpace = "Adobe RGB";
            else if (cs == 0xFFFF)
                rm.colorSpace = "Uncalibrated";
        }
        return true;
    case 0x828E: // CFAPattern
        if (rm.bayerPattern.empty() && e.count >= 4)
        {
            uint8_t cfa[4] = {0xFF, 0xFF, 0xFF, 0xFF};
            if (e.type == 1 && e.count <= 4)
            {
                cfa[0] = static_cast<uint8_t>(e.value & 0xFF);
                cfa[1] = static_cast<uint8_t>((e.value >> 8) & 0xFF);
                cfa[2] = static_cast<uint8_t>((e.value >> 16) & 0xFF);
                cfa[3] = static_cast<uint8_t>((e.value >> 24) & 0xFF);
            }
            else if (fits(e.value, 4, fileSize))
            {
                const long cur = std::ftell(f);
                if (std::fseek(f, static_cast<long>(e.value), SEEK_SET) == 0)
                {
                    if (std::fread(cfa, 1, 4, f) != 4)
                        std::memset(cfa, 0xFF, sizeof(cfa));
                }
                std::fseek(f, cur, SEEK_SET);
            }
            auto toChar = [](uint8_t v) -> char
            {
                if (v == 0)
                    return 'R';
                if (v == 1)
                    return 'G';
                if (v == 2)
                    return 'B';
                return '?';
            };
            if (cfa[0] <= 2 && cfa[1] <= 2 && cfa[2] <= 2 && cfa[3] <= 2)
            {
                rm.bayerPattern = {toChar(cfa[0]), toChar(cfa[1]), toChar(cfa[2]), toChar(cfa[3])};
            }
        }
        return true;
    default:
        return false;
    }
}

static bool hasAnyParsedTag(const RawMetadata &rm)
{
    return !rm.make.empty() || !rm.model.empty() || !rm.lens.empty() || rm.iso > 0 ||
           rm.exposureSec > 0.0 || rm.fNumber > 0.0 || rm.focalLength > 0.0 || rm.width > 0 ||
           rm.height > 0 || rm.bitsPerSample > 0 || !rm.colorSpace.empty() ||
           !rm.bayerPattern.empty() || rm.blackLevel > 0 || rm.whiteLevel > 0 ||
           rm.colorMatrixCount > 0 || rm.whiteBalanceCount > 0 || !rm.dateTime.empty() ||
           !rm.dateTimeOriginal.empty();
}

static void parseIfd(FILE *f, bool le, uint64_t fileSize, uint32_t ifdOffset, RawMetadata &rm,
                     std::vector<uint32_t> &visited, int depth = 0)
{
    if (depth > 4 || !fits(ifdOffset, 2, fileSize))
        return;
    for (uint32_t v : visited)
    {
        if (v == ifdOffset)
            return; // Cycle detected: stop recursion
    }
    visited.push_back(ifdOffset);

    if (std::fseek(f, static_cast<long>(ifdOffset), SEEK_SET) != 0)
        return;
    const uint16_t count = readU16(f, le);
    if (count == 0 || count > 4096)
        return;
    const uint64_t entriesBytes = static_cast<uint64_t>(count) * 12;
    if (!fits(ifdOffset + 2, entriesBytes, fileSize))
        return;

    uint32_t exifIfdOffset = 0;
    uint32_t subIfdOffset = 0;

    for (uint16_t i = 0; i < count; ++i)
    {
        IfdEntry e;
        if (!readEntry(f, le, e))
            break;

        if (e.tag == 0x8769) // Exif IFD Pointer
            exifIfdOffset = e.value;
        else if (e.tag == 0x014A) // SubIFD
            subIfdOffset = e.value;
        else
        {
            parseIdentityTag(f, fileSize, e, rm) || parseExposureTag(f, le, fileSize, e, rm) ||
                parseModeTag(f, le, fileSize, e, rm) ||
                parseCalibrationTag(f, le, fileSize, e, rm) ||
                parseSensorTag(f, le, fileSize, e, rm);
        }
    }

    // Next IFD pointer (at offset: ifdOffset + 2 + count * 12)
    uint32_t nextIfd = 0;
    const uint64_t nextIfdOffset = static_cast<uint64_t>(ifdOffset) + 2 + entriesBytes;
    if (fits(nextIfdOffset, 4, fileSize))
    {
        if (std::fseek(f, static_cast<long>(nextIfdOffset), SEEK_SET) == 0)
            nextIfd = readU32(f, le);
    }

    // Traverse Exif IFD (where standard cameras store exposure tags)
    if (exifIfdOffset > 0 && fits(exifIfdOffset, 2, fileSize))
        parseIfd(f, le, fileSize, exifIfdOffset, rm, visited, depth + 1);

    // Traverse SubIFD if present
    if (subIfdOffset > 0 && fits(subIfdOffset, 2, fileSize))
        parseIfd(f, le, fileSize, subIfdOffset, rm, visited, depth + 1);

    // Traverse Next IFD if top-level and not cyclic
    if (depth == 0 && nextIfd > 0 && fits(nextIfd, 2, fileSize) && visited.size() < 4)
        parseIfd(f, le, fileSize, nextIfd, rm, visited, depth);
}

} // namespace

RawMetadata parseRawMetadata(const std::string &filePath)
{
    RawMetadata rm;
    if (!isRawExtension(filePath))
        return rm;

    FILE *f = nullptr;
#ifdef _WIN32
    try
    {
        const std::filesystem::path nativePath = pathFromUtf8(filePath);
        f = _wfopen(nativePath.native().c_str(), L"rb");
    }
    catch (...)
    {
        return rm;
    }
#else
    f = std::fopen(filePath.c_str(), "rb");
#endif
    if (!f)
        return rm;

    if (std::fseek(f, 0, SEEK_END) != 0)
    {
        std::fclose(f);
        return rm;
    }
    const long endPos = std::ftell(f);
    if (endPos <= 0)
    {
        std::fclose(f);
        return rm;
    }
    const uint64_t fileSize = static_cast<uint64_t>(endPos);
    if (std::fseek(f, 0, SEEK_SET) != 0)
    {
        std::fclose(f);
        return rm;
    }

    TiffHeader h;
    if (!readTiffHeader(f, fileSize, h))
    {
        std::fclose(f);
        return rm;
    }

    std::vector<uint32_t> visited;
    parseIfd(f, h.littleEndian, fileSize, h.ifdOffset, rm, visited, 0);
    std::fclose(f);

    rm.parsed = hasAnyParsedTag(rm);
    return rm;
}

} // namespace mviewer::core
