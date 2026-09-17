#include "core/image/IccProfile.h"

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>

namespace mviewer::core
{
namespace
{

inline uint32_t be32(const unsigned char *p)
{
    return (uint32_t(p[0]) << 24) | (uint32_t(p[1]) << 16) | (uint32_t(p[2]) << 8) | uint32_t(p[3]);
}

inline uint16_t be16(const unsigned char *p)
{
    return (uint16_t(p[0]) << 8) | uint16_t(p[1]);
}

inline std::string sig4(const unsigned char *p)
{
    // Pointer-pair form avoids a reinterpret_cast (the CI clang-tidy check set
    // promotes cppcoreguidelines-pro-type-reinterpret-cast to an error).
    return std::string(p, p + 4);
}

inline std::string trimNull(const std::string &s)
{
    const size_t n = s.find('\0');
    return n == std::string::npos ? s : s.substr(0, n);
}

// Decode a desc/text/mluc tag payload into a UTF-8 string.
std::string parseTextType(const unsigned char *tagData, uint32_t tagSize)
{
    if (tagSize < 4)
        return {};
    const std::string type = sig4(tagData);
    if (type == "desc")
    {
        // desc type: bytes 4-7 reserved, 8-11 ASCII length, then the string.
        if (tagSize < 12)
            return {};
        uint32_t len = be32(tagData + 8);
        const uint32_t maxLen = tagSize - 12;
        if (len > maxLen)
            len = maxLen;
        return trimNull(std::string(tagData + 12, tagData + 12 + len));
    }
    if (type == "text")
    {
        // text type (ICC.1:2010 §10.22): bytes 4-7 reserved, 8..end 7-bit ASCII string.
        // There is no 4-byte length field at 8-11 in textType.
        if (tagSize < 8)
            return {};
        return trimNull(std::string(tagData + 8, tagData + tagSize));
    }
    if (type == "mluc")
    {
        // mluc type: records of UTF-16BE strings. Reading the record size at
        // offset 12 needs 16 bytes present, not 12.
        if (tagSize < 16)
            return {};
        const uint32_t recSize = be32(tagData + 12);
        if (recSize < 12 || tagSize < 16 + recSize)
            return {};
        // Bound the record count by what the tag payload can actually hold so a
        // hostile/malformed recCount cannot spin this loop forever (or overflow).
        const uint32_t maxRec = (tagSize - 16) / recSize;
        const uint32_t recCount = be32(tagData + 8);
        for (uint32_t i = 0; i < recCount && i < maxRec; ++i)
        {
            const uint32_t recOff = 16 + i * recSize;
            // 16 bytes are needed, not 12: the string offset is read at
            // recOff + 12, so a payload ending exactly at recOff + 12 would
            // otherwise be over-read by 4 bytes.
            if (recOff + 16 > tagSize)
                break;
            const uint32_t len = be32(tagData + recOff + 8);
            const uint32_t strOff = be32(tagData + recOff + 12);
            // Subtraction form: `strOff + len` wraps in 32-bit arithmetic for a
            // hostile offset (0xFFFFFFFE + 2 == 0), which previously let a
            // crafted profile read gigabytes past the tag payload.
            if (len < 2 || strOff > tagSize || len > tagSize - strOff)
                continue;
            std::string out;
            for (uint32_t j = 0; j + 1 < len; j += 2)
            {
                const uint16_t ch = be16(tagData + strOff + j);
                if (ch == 0)
                    break;
                if (ch < 0x80)
                    out.push_back(static_cast<char>(ch));
                else if (ch < 0x800)
                {
                    out.push_back(static_cast<char>(0xC0 | (ch >> 6)));
                    out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                }
                else
                {
                    out.push_back(static_cast<char>(0xE0 | (ch >> 12)));
                    out.push_back(static_cast<char>(0x80 | ((ch >> 6) & 0x3F)));
                    out.push_back(static_cast<char>(0x80 | (ch & 0x3F)));
                }
            }
            if (!out.empty())
                return out;
        }
        return {};
    }
    return {};
}

const char *deviceClassText(const std::string &s)
{
    if (s == "scnr")
        return "扫描仪";
    if (s == "mntr")
        return "显示器";
    if (s == "prtr")
        return "打印机";
    if (s == "link")
        return "设备链接";
    if (s == "abst")
        return "抽象";
    if (s == "spac")
        return "色彩空间";
    if (s == "nmcl")
        return "命名色";
    if (s == "camb")
        return "相机";
    if (s == "vidm")
        return "视频";
    return "未知";
}

const char *colorSpaceText(const std::string &s)
{
    if (s == "RGB " || s.rfind("RGB", 0) == 0)
        return "RGB";
    if (s == "GRAY" || s.rfind("GRAY", 0) == 0)
        return "灰度";
    if (s == "CMYK")
        return "CMYK";
    if (s == "CMY " || s.rfind("CMY", 0) == 0)
        return "CMY";
    if (s == "Lab " || s.rfind("Lab", 0) == 0)
        return "Lab";
    if (s == "XYZ " || s.rfind("XYZ", 0) == 0)
        return "XYZ";
    if (s == "Luv " || s.rfind("Luv", 0) == 0)
        return "Luv";
    if (s == "Yxy " || s.rfind("Yxy", 0) == 0)
        return "Yxy";
    if (s == "HSV " || s.rfind("HSV", 0) == 0)
        return "HSV";
    if (s == "HLS " || s.rfind("HLS", 0) == 0)
        return "HLS";
    if (s == "YCbr")
        return "YCbCr";
    return "未知";
}

const char *renderingIntentText(uint32_t v)
{
    switch (v)
    {
    case 0:
        return "感知 (Perceptual)";
    case 1:
        return "相对色度 (Media-Relative Colorimetric)";
    case 2:
        return "饱和度 (Saturation)";
    case 3:
        return "绝对色度 (ICC-Absolute Colorimetric)";
    default:
        return "未知";
    }
}

} // namespace

IccProfile parseIccProfile(const unsigned char *data, size_t size)
{
    IccProfile info;
    // Need the 128-byte header plus the 4-byte tag count.
    if (!data || size < 132)
        return info;
    info.valid = true;

    // ICC version: byte8 = major (binary); byte9 = minor in the BCD high nibble;
    // byte10 = bugfix tens digit; byte11 = bugfix ones digit. The minor byte is
    // BCD (e.g. 0x10 means minor 1), so only the high nibble is the minor number.
    const uint32_t major = data[8];
    const uint32_t minor = data[9] >> 4;
    const uint32_t bugfix = uint32_t(data[10]) * 10 + uint32_t(data[11]);
    info.version =
        std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(bugfix);
    info.deviceClass = deviceClassText(sig4(data + 12));
    info.colorSpace = colorSpaceText(sig4(data + 16));
    info.pcs = colorSpaceText(sig4(data + 20));
    info.renderingIntent = renderingIntentText(be32(data + 64));

    const uint32_t maxTags = (size >= 144) ? static_cast<uint32_t>((size - 132) / 12) : 0;
    const uint32_t tagCount = std::min(be32(data + 128), maxTags);
    for (uint32_t i = 0; i < tagCount; ++i)
    {
        const unsigned char *e = data + 132 + size_t(i) * 12;
        const std::string sig = sig4(e);
        const uint32_t off = be32(e + 4);
        const uint32_t sz = be32(e + 8);
        if (sz < 4 || sz > size || off > size - sz)
            continue;
        const unsigned char *tagData = data + off;
        if (sig == "desc" || sig == "dscm" || (info.description.empty() && sig == "dmdd"))
        {
            if (info.description.empty())
                info.description = parseTextType(tagData, sz);
        }
        else if (sig == "cprt")
        {
            if (info.copyright.empty())
                info.copyright = parseTextType(tagData, sz);
        }
    }
    return info;
}

} // namespace mviewer::core
