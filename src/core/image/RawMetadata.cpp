#include "core/image/RawMetadata.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageFormats.h"
#include "core/image/RawTiffIo.h"

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <iterator>
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

using namespace raw_tiff;

static bool parseCameraIdentityTag(FILE *f, uint64_t fileSize, const IfdEntry &e, RawMetadata &rm)
{
    switch (e.tag)
    {
    case 0x010F:
        if (e.type == 2 && e.count > 0 && rm.make.empty())
            rm.make = readString(f, fileSize, e);
        return true;
    case 0x0110:
        if (e.type == 2 && e.count > 0 && rm.model.empty())
            rm.model = readString(f, fileSize, e);
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
            rm.description = readString(f, fileSize, e);
        return true;
    case 0x0132:
        if (e.type == 2 && e.count > 0 && rm.dateTime.empty())
            rm.dateTime = readString(f, fileSize, e);
        return true;
    case 0x9003:
        if (e.type == 2 && e.count > 0 && rm.dateTimeOriginal.empty())
            rm.dateTimeOriginal = readString(f, fileSize, e);
        return true;
    case 0x9286:
        if (e.type == 7 && e.count > 0 && rm.description.empty())
            rm.description = readString(f, fileSize, e);
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
            rm.serialNumber = readString(f, fileSize, e);
        return true;
    case 0xA433:
        if (e.type == 2 && e.count > 0 && rm.lensMaker.empty())
            rm.lensMaker = readString(f, fileSize, e);
        return true;
    case 0xA434:
        if (e.type == 2 && e.count > 0 && rm.lens.empty())
            rm.lens = readString(f, fileSize, e);
        return true;
    case 0xA435:
        if (e.type == 2 && e.count > 0 && rm.serialNumber.empty())
            rm.serialNumber = readString(f, fileSize, e);
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

// EXIF exposure rationals (type 5, count 1). Each field keeps its first value;
// ShutterSpeedValue additionally ignores non-positive readings.
struct ExposureRationalField
{
    double RawMetadata::*field;
    uint16_t tag;
    bool positiveOnly;
};

static constexpr ExposureRationalField kExposureRationalFields[] = {
    {&RawMetadata::exposureSec, 0x829A, false},
    {&RawMetadata::fNumber, 0x829D, false},
    {&RawMetadata::shutterSpeedValue, 0x9201, true},
    {&RawMetadata::apertureValue, 0x9202, false},
    {&RawMetadata::brightnessValue, 0x9203, false},
    {&RawMetadata::exposureCompensation, 0x9204, false},
};

static bool parseExposureRationalTag(FILE *f, bool le, uint64_t fileSize, const IfdEntry &e,
                                     RawMetadata &rm)
{
    const auto *spec =
        std::find_if(std::begin(kExposureRationalFields), std::end(kExposureRationalFields),
                     [&e](const ExposureRationalField &r) { return r.tag == e.tag; });
    if (spec == std::end(kExposureRationalFields))
        return false;
    double &field = rm.*(spec->field);
    // ShutterSpeedValue, BrightnessValue and ExposureBiasValue are SRATIONAL.
    if ((e.type != 5 && e.type != 10) || e.count != 1 || field != 0.0)
        return true;
    const double value = e.type == 10 ? readSRational(f, le, fileSize, e.value)
                                      : readRational(f, le, fileSize, e.value);
    if (!spec->positiveOnly || value > 0.0)
        field = value;
    return true;
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
                // Inline BYTEs are in file order regardless of II/MM.
                std::memcpy(cfa, e.raw, 4);
            }
            else if (fits(e.value, 4, fileSize))
            {
                const int64_t cur = tell64(f);
                if (seekTiff(f, e.value) == 0)
                {
                    if (std::fread(cfa, 1, 4, f) != 4)
                        std::memset(cfa, 0xFF, sizeof(cfa));
                }
                seek64(f, cur, SEEK_SET);
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

    if (seekTiff(f, ifdOffset) != 0)
        return;
    const uint16_t count = readU16(f, le);
    if (count == 0 || count > 4096)
        return;
    const uint64_t entriesBytes = static_cast<uint64_t>(count) * 12;
    if (!fits(ifdOffset + 2, entriesBytes, fileSize))
        return;

    uint32_t exifIfdOffset = 0;
    IfdEntry subIfdEntry;
    bool hasSubIfd = false;

    for (uint16_t i = 0; i < count; ++i)
    {
        IfdEntry e;
        if (!readEntry(f, le, e))
            break;

        if (e.tag == 0x8769) // Exif IFD Pointer
            exifIfdOffset = e.value;
        else if (e.tag == 0x014A) // SubIFD (LONG or IFD, count >= 1)
        {
            subIfdEntry = e;
            hasSubIfd = true;
        }
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
        if (seekTiff(f, nextIfdOffset) == 0)
            nextIfd = readU32(f, le);
    }

    // Traverse Exif IFD (where standard cameras store exposure tags)
    if (exifIfdOffset > 0 && fits(exifIfdOffset, 2, fileSize))
        parseIfd(f, le, fileSize, exifIfdOffset, rm, visited, depth + 1);

    // Traverse every SubIFD (DNG/NEF store several: preview, full raw, ...).
    // count == 1 keeps the offset inline; count > 1 points at an offset array.
    if (hasSubIfd && subIfdEntry.count > 0)
    {
        constexpr uint32_t kMaxSubIfds = 8;
        std::vector<uint32_t> subIfds;
        if (subIfdEntry.count == 1)
            subIfds.push_back(subIfdEntry.value);
        else if (fits(subIfdEntry.value, uint64_t{4} * std::min(subIfdEntry.count, kMaxSubIfds),
                      fileSize) &&
                 seekTiff(f, subIfdEntry.value) == 0)
        {
            for (uint32_t k = 0; k < subIfdEntry.count && k < kMaxSubIfds; ++k)
                subIfds.push_back(readU32(f, le));
        }
        for (const uint32_t sub : subIfds)
        {
            if (sub > 0 && fits(sub, 2, fileSize))
                parseIfd(f, le, fileSize, sub, rm, visited, depth + 1);
        }
    }

    // Traverse Next IFD if top-level and not cyclic
    if (depth == 0 && nextIfd > 0 && fits(nextIfd, 2, fileSize) && visited.size() < 4)
        parseIfd(f, le, fileSize, nextIfd, rm, visited, depth);
}

} // namespace

namespace
{

FILE *openUtf8(const std::string &filePath)
{
#ifdef _WIN32
    try
    {
        const std::filesystem::path nativePath = pathFromUtf8(filePath);
        return _wfopen(nativePath.native().c_str(), L"rb");
    }
    catch (...)
    {
        return nullptr;
    }
#else
    return std::fopen(filePath.c_str(), "rb");
#endif
}

uint64_t fileSizeOf(FILE *f)
{
    if (seek64(f, 0, SEEK_END) != 0)
        return 0;
    const int64_t endPos = tell64(f);
    if (endPos <= 0 || seek64(f, 0, SEEK_SET) != 0)
        return 0;
    return static_cast<uint64_t>(endPos);
}

// Parses the TIFF structure starting at file position `base` (`size` bytes).
void parseTiffAt(FILE *f, int64_t base, uint64_t size, RawMetadata &rm)
{
    t_tiffBase = base;
    TiffHeader h;
    if (seek64(f, base, SEEK_SET) == 0 && readTiffHeader(f, size, h))
    {
        std::vector<uint32_t> visited;
        parseIfd(f, h.littleEndian, size, h.ifdOffset, rm, visited, 0);
    }
    t_tiffBase = 0;
    rm.parsed = hasAnyParsedTag(rm);
}

// Finds the TIFF payload of the JPEG APP1 "Exif" segment. Header-only walk:
// stops at SOS, never touches entropy-coded data.
bool findJpegExif(FILE *f, uint64_t fileSize, int64_t &base, uint64_t &size)
{
    uint8_t b[4];
    if (std::fread(b, 1, 2, f) != 2 || b[0] != 0xFF || b[1] != 0xD8)
        return false;
    for (int guard = 0; guard < 256; ++guard)
    {
        if (std::fread(b, 1, 2, f) != 2 || b[0] != 0xFF)
            return false;
        if (b[1] == 0xFF)
        {
            std::fseek(f, -1, SEEK_CUR);
            continue;
        }
        if (b[1] == 0xD9 || b[1] == 0xDA)
            return false;
        if (b[1] == 0x01 || (b[1] >= 0xD0 && b[1] <= 0xD7))
            continue;
        const uint8_t code = b[1];
        if (std::fread(b, 1, 2, f) != 2)
            return false;
        const int len = (b[0] << 8) | b[1];
        if (len < 2)
            return false;
        const int64_t payloadPos = tell64(f);
        if (!fits(static_cast<uint64_t>(payloadPos), static_cast<uint64_t>(len - 2), fileSize))
            return false;
        char id[6] = {};
        if (code == 0xE1 && len >= 16 && std::fread(id, 1, 6, f) == 6 &&
            std::memcmp(id, "Exif\0\0", 6) == 0)
        {
            base = payloadPos + 6;
            size = static_cast<uint64_t>(len - 2 - 6);
            return true;
        }
        if (seek64(f, payloadPos + len - 2, SEEK_SET) != 0)
            return false;
    }
    return false;
}

bool isJpegExtension(const std::string &path)
{
    const std::string ext = toLower(pathToUtf8(pathFromUtf8(path).extension()));
    return ext == ".jpg" || ext == ".jpeg" || ext == ".jpe";
}

} // namespace

RawMetadata parseRawMetadata(const std::string &filePath)
{
    RawMetadata rm;
    if (!isRawPath(filePath))
        return rm;
    FILE *f = openUtf8(filePath);
    if (!f)
        return rm;
    const uint64_t fileSize = fileSizeOf(f);
    if (fileSize > 0)
        parseTiffAt(f, 0, fileSize, rm);
    std::fclose(f);
    return rm;
}

RawMetadata parseCaptureMetadata(const std::string &filePath)
{
    if (isRawPath(filePath))
        return parseRawMetadata(filePath);
    RawMetadata rm;
    if (!isJpegExtension(filePath))
        return rm;
    FILE *f = openUtf8(filePath);
    if (!f)
        return rm;
    const uint64_t fileSize = fileSizeOf(f);
    int64_t base = 0;
    uint64_t size = 0;
    if (fileSize > 0 && findJpegExif(f, fileSize, base, size))
        parseTiffAt(f, base, size, rm);
    std::fclose(f);
    return rm;
}

} // namespace mviewer::core
