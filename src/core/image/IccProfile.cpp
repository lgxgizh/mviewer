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
    return std::string(reinterpret_cast<const char *>(p), 4);
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
    if (type == "desc" || type == "text")
    {
        // desc type: bytes 4-7 reserved, 8-11 ASCII length, then the string.
        if (tagSize < 12)
            return {};
        uint32_t len = be32(tagData + 8);
        const uint32_t maxLen = tagSize - 12;
        if (len > maxLen)
            len = maxLen;
        return trimNull(std::string(reinterpret_cast<const char *>(tagData + 12), len));
    }
    if (type == "mluc")
    {
        // mluc type: records of UTF-16BE strings.
        if (tagSize < 12)
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
            if (recOff + 12 > tagSize)
                break;
            const uint32_t len = be32(tagData + recOff + 8);
            const uint32_t strOff = be32(tagData + recOff + 12);
            if (strOff + len > tagSize || len < 2)
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
    if (s == "RGB ")
        return "RGB";
    if (s == "GRAY")
        return "灰度";
    if (s == "CMYK")
        return "CMYK";
    if (s == "CMY ")
        return "CMY";
    if (s == "Lab ")
        return "Lab";
    if (s == "XYZ ")
        return "XYZ";
    if (s == "Luv ")
        return "Luv";
    if (s == "Yxy ")
        return "Yxy";
    if (s == "HSV ")
        return "HSV";
    if (s == "HLS ")
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
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%u.%u.%u", major, minor, bugfix);
    info.version = buf;
    info.deviceClass = deviceClassText(sig4(data + 12));
    info.colorSpace = colorSpaceText(sig4(data + 16));
    info.pcs = colorSpaceText(sig4(data + 20));
    info.renderingIntent = renderingIntentText(be32(data + 64));

    const uint32_t tagCount = be32(data + 128);
    for (uint32_t i = 0; i < tagCount; ++i)
    {
        const size_t entryEnd = 132 + size_t(i) * 12 + 12;
        if (entryEnd > size)
            break;
        const unsigned char *e = data + 132 + i * 12;
        const std::string sig = sig4(e);
        const uint32_t off = be32(e + 4);
        const uint32_t sz = be32(e + 8);
        if (sz < 4 || sz > size || off > size - sz)
            continue;
        const unsigned char *tagData = data + off;
        if (sig == "desc")
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
