#include "core/image/RawMetadata.h"

#include <Windows.h>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <string>

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
    std::filesystem::path p(path);
    std::string ext = toLower(p.extension().string());
    return ext == ".dng" || ext == ".arw" || ext == ".nef" || ext == ".orf" ||
           ext == ".rw2" || ext == ".raf" || ext == ".cr2" || ext == ".cr3" ||
           ext == ".crw" || ext == ".pef" || ext == ".srw" || ext == ".x3f" ||
           ext == ".erf" || ext == ".mos" || ext == ".raw";
}

struct TiffHeader
{
    bool littleEndian = false;
    uint32_t ifdOffset = 0;
};

static bool readTiffHeader(FILE *f, TiffHeader &h)
{
    uint8_t buf[8];
    if (std::fread(buf, 1, 8, f) != 8) return false;
    if (buf[0] == 'I' && buf[1] == 'I') h.littleEndian = true;
    else if (buf[0] == 'M' && buf[1] == 'M') h.littleEndian = false;
    else return false;
    uint16_t magic = h.littleEndian ? (buf[2] | (buf[3] << 8)) : ((buf[2] << 8) | buf[3]);
    if (magic != 42) return false;
    h.ifdOffset = h.littleEndian ? (buf[4] | (buf[5] << 8) | (buf[6] << 16) | (buf[7] << 24))
                                 : ((buf[4] << 24) | (buf[5] << 16) | (buf[6] << 8) | buf[7]);
    return true;
}

static uint16_t readU16(FILE *f, bool le)
{
    uint8_t b[2];
    if (std::fread(b, 1, 2, f) != 2) return 0;
    return le ? (b[0] | (b[1] << 8)) : ((b[0] << 8) | b[1]);
}

static uint32_t readU32(FILE *f, bool le)
{
    uint8_t b[4];
    if (std::fread(b, 1, 4, f) != 4) return 0;
    return le ? (b[0] | (b[1] << 8) | (b[2] << 16) | (b[3] << 24))
              : ((b[0] << 24) | (b[1] << 16) | (b[2] << 8) | b[3]);
}

static bool seek(FILE *f, long offset) { return std::fseek(f, offset, SEEK_SET) == 0; }

struct IfdEntry
{
    uint16_t tag = 0;
    uint16_t type = 0;
    uint32_t count = 0;
    uint32_t value = 0;
};

static IfdEntry readEntry(FILE *f, bool le)
{
    IfdEntry e;
    e.tag = readU16(f, le);
    e.type = readU16(f, le);
    e.count = readU32(f, le);
    e.value = readU32(f, le);
    return e;
}

static std::string readString(FILE *f, bool le, uint32_t offset, uint32_t count)
{
    if (count == 0 || count > 4096) return "";
    long cur = std::ftell(f);
    if (!seek(f, offset)) return "";
    std::string s(count, '\0');
    if (std::fread(s.data(), 1, count, f) != count) { seek(f, cur); return ""; }
    seek(f, cur);
    while (!s.empty() && s.back() == '\0') s.pop_back();
    return s;
}

static uint16_t readTagU16(FILE *f, bool le, uint32_t v, uint16_t type, uint32_t count)
{
    if (type == 3 && count <= 2) return static_cast<uint16_t>(v & 0xFFFF);
    long cur = std::ftell(f);
    if (!seek(f, v)) return 0;
    uint16_t r = readU16(f, le);
    seek(f, cur);
    return r;
}

static uint32_t readTagU32(FILE *f, bool le, uint32_t v, uint16_t type, uint32_t count)
{
    if (type == 4 && count == 1) return v;
    long cur = std::ftell(f);
    if (!seek(f, v)) return 0;
    uint32_t r = readU32(f, le);
    seek(f, cur);
    return r;
}

static double readRational(FILE *f, bool le, uint32_t offset)
{
    long cur = std::ftell(f);
    if (!seek(f, offset)) return 0.0;
    uint32_t num = readU32(f, le);
    uint32_t den = readU32(f, le);
    seek(f, cur);
    return den == 0 ? 0.0 : static_cast<double>(num) / static_cast<double>(den);
}

static void parseIfd(FILE *f, bool le, uint32_t ifdOffset, RawMetadata &rm)
{
    if (!seek(f, ifdOffset)) return;
    uint16_t count = readU16(f, le);
    for (uint16_t i = 0; i < count; ++i)
    {
        auto e = readEntry(f, le);
        switch (e.tag)
        {
        case 0x010E:
            if (e.type == 2 && e.count > 0 && rm.description.empty())
                rm.description = readString(f, le, e.value, e.count);
            break;
        case 0x010F:
            if (e.type == 2 && e.count > 0) rm.make = readString(f, le, e.value, e.count);
            break;
        case 0x0110:
            if (e.type == 2 && e.count > 0) rm.model = readString(f, le, e.value, e.count);
            break;
        case 0x0132:
            if (e.type == 2 && e.count > 0 && rm.dateTime.empty())
                rm.dateTime = readString(f, le, e.value, e.count);
            break;
        case 0x829A:
            if (e.type == 5 && e.count == 1) rm.exposureSec = readRational(f, le, e.value);
            break;
        case 0x829D:
            if (e.type == 5 && e.count == 1) rm.fNumber = readRational(f, le, e.value);
            break;
        case 0x8827:
            if (e.type == 3 && e.count >= 1) rm.isoSpeed = readTagU16(f, le, e.value, e.type, e.count);
            break;
        case 0x9003:
            if (e.type == 2 && e.count > 0 && rm.dateTimeOriginal.empty())
                rm.dateTimeOriginal = readString(f, le, e.value, e.count);
            break;
        case 0x9201:
            if (e.type == 5 && e.count == 1)
            {
                double sv = readRational(f, le, e.value);
                if (sv > 0.0) rm.shutterSpeedValue = sv;
            }
            break;
        case 0x9202:
            if (e.type == 5 && e.count == 1) rm.apertureValue = readRational(f, le, e.value);
            break;
        case 0x9203:
            if (e.type == 5 && e.count == 1) rm.brightnessValue = readRational(f, le, e.value);
            break;
        case 0x9204:
            if (e.type == 5 && e.count == 1) rm.exposureCompensation = readRational(f, le, e.value);
            break;
        case 0x9207:
            if (e.type == 3 && e.count == 1)
            {
                uint16_t v = readTagU16(f, le, e.value, e.type, e.count);
                switch (v)
                {
                case 1: rm.meteringMode = "Average"; break;
                case 2: rm.meteringMode = "Center-weighted average"; break;
                case 3: rm.meteringMode = "Spot"; break;
                case 4: rm.meteringMode = "Multi-spot"; break;
                case 5: rm.meteringMode = "Multi-segment"; break;
                case 6: rm.meteringMode = "Partial"; break;
                case 255: rm.meteringMode = "Other"; break;
                default: rm.meteringMode = "Unknown"; break;
                }
            }
            break;
        case 0x9208:
            if (e.type == 3 && e.count == 1)
            {
                uint16_t v = readTagU16(f, le, e.value, e.type, e.count);
                if (v & 1) rm.flash = "Fired"; else rm.flash = "Did not fire";
            }
            break;
        case 0x920A:
            if (e.type == 5 && e.count == 1) rm.focalLength = readRational(f, le, e.value);
            break;
        case 0x9286:
            if (e.type == 7 && e.count > 0 && rm.description.empty())
                rm.description = readString(f, le, e.value, e.count);
            break;
        case 0xA217:
            if (e.type == 3 && e.count == 1)
            {
                uint16_t v = readTagU16(f, le, e.value, e.type, e.count);
                if (v == 2) rm.sensorType = "Color Filter Array";
                else if (v == 3) rm.sensorType = "Color Filter Array (single-chip)";
                else if (v == 1) rm.sensorType = "Not defined";
                else rm.sensorType = "Other";
            }
            break;
        case 0xA402:
            if (e.type == 3 && e.count == 1)
            {
                uint16_t v = readTagU16(f, le, e.value, e.type, e.count);
                switch (v)
                {
                case 0: rm.exposureProgram = "Auto"; break;
                case 1: rm.exposureProgram = "Manual"; break;
                case 2: rm.exposureProgram = "Auto bracket"; break;
                default: rm.exposureProgram = "Unknown"; break;
                }
            }
            break;
        case 0xA403:
            if (e.type == 3 && e.count == 1)
            {
                uint16_t v = readTagU16(f, le, e.value, e.type, e.count);
                switch (v)
                {
                case 0: rm.whiteBalance = "Auto"; break;
                case 1: rm.whiteBalance = "Manual"; break;
                default: rm.whiteBalance = "Unknown"; break;
                }
            }
            break;
        case 0xA405:
            if (e.type == 3 && e.count == 1)
                rm.focalLength35mm = static_cast<double>(readTagU16(f, le, e.value, e.type, e.count));
            break;
        case 0xA431:
            if (e.type == 2 && e.count > 0) rm.lens = readString(f, le, e.value, e.count);
            break;
        case 0xA433:
            if (e.type == 2 && e.count > 0) rm.lensMaker = readString(f, le, e.value, e.count);
            break;
        case 0xA434:
            if (e.type == 2 && e.count > 0 && rm.lens.empty())
                rm.lens = readString(f, le, e.value, e.count);
            break;
        case 0xA435:
            if (e.type == 2 && e.count > 0)
                rm.serialNumber = readString(f, le, e.value, e.count);
            break;
        case 0xC614:
            if (e.type == 5 && e.count <= 12)
            {
                for (uint32_t k = 0; k < e.count && k < 12; ++k)
                {
                    long cur = std::ftell(f);
                    rm.colorMatrix[k] = readRational(f, le, e.value + k * 8);
                    seek(f, cur);
                }
                rm.colorMatrixCount = (e.count < 12u) ? e.count : 12u;
            }
            break;
        case 0xC61A:
            if (e.type == 3 && e.count >= 1) rm.blackLevel = readTagU16(f, le, e.value, e.type, e.count);
            break;
        case 0xC61B:
            if (e.type == 3 && e.count >= 1) rm.whiteLevel = readTagU16(f, le, e.value, e.type, e.count);
            break;
        case 0xC628:
            if (e.type == 5 && e.count <= 4)
            {
                for (uint32_t k = 0; k < e.count && k < 4; ++k)
                    rm.whiteBalanceRGGB[k] = readRational(f, le, e.value + k * 8);
                rm.whiteBalanceCount = (e.count < 4u) ? e.count : 4u;
            }
            break;
        default:
            break;
        }
    }
    rm.parsed = true;
}

} // namespace

RawMetadata parseRawMetadata(const std::string &filePath)
{
    RawMetadata rm;
    if (!isRawExtension(filePath)) return rm;

    FILE *f = nullptr;
#ifdef _WIN32
    int len = MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, nullptr, 0);
    if (len > 0)
    {
        std::wstring wpath(len, L'\0');
        MultiByteToWideChar(CP_UTF8, 0, filePath.c_str(), -1, wpath.data(), len);
        f = _wfopen(wpath.c_str(), L"rb");
    }
#else
    f = std::fopen(filePath.c_str(), "rb");
#endif
    if (!f) return rm;

    TiffHeader h;
    if (!readTiffHeader(f, h)) { std::fclose(f); return rm; }

    parseIfd(f, h.littleEndian, h.ifdOffset, rm);
    std::fclose(f);
    return rm;
}

} // namespace mviewer::core
