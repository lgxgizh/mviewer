#include "core/image/decoder/HdrDecoder.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/decoder/HdrDecoderInternal.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <limits>
#include <sstream>
#include <string>
#include <vector>

namespace
{
namespace cap = mviewer::core::hdr_detail;

static_assert(cap::kMaxExrInputBytes <=
              static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()));
static_assert(0x100000000ull > cap::kMaxExrInputBytes);
static_assert(cap::kMaxHdrEdge > 0);

bool hasExtension(const std::string &path, const char *ext)
{
    const size_t len = std::strlen(ext);
    if (path.size() <= len)
        return false;
    const size_t start = path.size() - len;
    if (path[start - 1] != '.')
        return false;
    for (size_t i = 0; i < len; ++i)
    {
        char c = path[start + i];
        if (c >= 'A' && c <= 'Z')
            c = static_cast<char>(c - 'A' + 'a');
        if (c != ext[i])
            return false;
    }
    return true;
}

float halfToFloat(uint16_t h)
{
    const uint32_t s = (static_cast<uint32_t>(h) & 0x8000u) << 16;
    const uint32_t e = (static_cast<uint32_t>(h) >> 10) & 0x1Fu;
    const uint32_t m = static_cast<uint32_t>(h) & 0x3FFu;
    uint32_t out;
    if (e == 0)
    {
        if (m == 0)
        {
            out = s;
        }
        else
        {
            int shift = 0;
            uint32_t mm = m;
            while ((mm & 0x400u) == 0)
            {
                mm <<= 1;
                ++shift;
            }
            mm &= 0x3FFu;
            out = s | ((127 - 15 - shift + 1) << 23) | (mm << 13);
        }
    }
    else if (e == 31)
    {
        out = s | 0x7F800000u | (m << 13);
    }
    else
    {
        out = s | ((e - 15 + 127) << 23) | (m << 13);
    }
    return std::bit_cast<float>(out);
}

// A final line without a newline is kept. Overlong input stops at the cap.
bool readHeaderLine(std::istream &in, std::string &line, std::string &error)
{
    line.clear();
    char c = 0;
    bool any = false;
    while (in.get(c))
    {
        any = true;
        if (c == '\n')
        {
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return true;
        }
        if (line.size() >= cap::kMaxPfmHeaderLine)
        {
            error = "PFM header line is too long";
            return false;
        }
        line.push_back(c);
    }
    if (in.bad())
    {
        error = "cannot read PFM header";
        return false;
    }
    if (!any)
    {
        error = "truncated PFM header";
        return false;
    }
    if (!line.empty() && line.back() == '\r')
        line.pop_back();
    return true;
}

// Reject above the input cap, read that many bytes, and probe one unstored byte.
bool readBoundedExr(std::ifstream &in, std::vector<std::uint8_t> &file, std::string &error)
{
    file.clear();
    in.seekg(0, std::ios::end);
    const std::streamoff length = in.tellg();
    if (length < 0)
    {
        error = "cannot size OpenEXR input";
        return false;
    }
    const auto bytes = static_cast<std::uint64_t>(length);
    if (bytes > cap::kMaxExrInputBytes ||
        bytes > static_cast<std::uint64_t>((std::numeric_limits<size_t>::max)()) ||
        bytes > static_cast<std::uint64_t>((std::numeric_limits<std::streamsize>::max)()))
    {
        error = "OpenEXR input exceeds the read limit";
        return false;
    }
    in.seekg(0, std::ios::beg);
    if (!in)
    {
        error = "cannot rewind OpenEXR input";
        return false;
    }
    file.resize(static_cast<size_t>(bytes));
    if (bytes != 0)
    {
        in.read(reinterpret_cast<char *>(file.data()), static_cast<std::streamsize>(bytes));
        if (static_cast<std::uint64_t>(in.gcount()) != bytes)
        {
            file.clear();
            error = "short OpenEXR read";
            return false;
        }
    }
    if (in.get() != std::char_traits<char>::eof())
    {
        file.clear();
        error = "OpenEXR input grew during read";
        return false;
    }
    in.clear();
    return true;
}

bool exrMagic(const std::vector<std::uint8_t> &file)
{
    return file.size() >= 12 && file[0] == 0x76 && file[1] == 0x2f && file[2] == 0x31 &&
           file[3] == 0x01;
}

uint32_t readU32(const std::uint8_t *p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) |
           (static_cast<uint32_t>(p[2]) << 16) | (static_cast<uint32_t>(p[3]) << 24);
}

uint64_t readU64(const std::uint8_t *p)
{
    uint64_t v = 0;
    for (int i = 7; i >= 0; --i)
        v = (v << 8) | static_cast<uint64_t>(p[static_cast<size_t>(i)]);
    return v;
}

int32_t readI32(const std::uint8_t *p)
{
    return static_cast<int32_t>(readU32(p));
}

float readF32(const std::uint8_t *p)
{
    const uint32_t bits = readU32(p);
    return std::bit_cast<float>(bits);
}

float loadPfmSample(const std::uint8_t *p, bool littleEndian, float scaleFactor)
{
    std::array<std::uint8_t, 4> bytes{};
    for (int b = 0; b < 4; ++b)
        bytes[static_cast<size_t>(b)] = p[b];
    if (!littleEndian)
    {
        std::swap(bytes[0], bytes[3]);
        std::swap(bytes[1], bytes[2]);
    }
    return readF32(bytes.data()) * scaleFactor;
}

struct ExrChannel
{
    char name[8] = {};
    int pixelType = 2; // 0=UINT, 1=HALF, 2=FLOAT
};

struct ExrHeader
{
    int minX = 0, minY = 0, maxX = -1, maxY = -1;
    int compression = 0;
    int lineOrder = 0;
    std::vector<ExrChannel> channels;
};

// Decode one uncompressed scanline block into a planar float line.
bool decodeExrScanline(const std::uint8_t *data, int32_t dataSize,
                       const std::vector<ExrChannel> &channels, size_t pixelsPerLine,
                       std::vector<float> &line)
{
    size_t at = 0;
    for (size_t c = 0; c < channels.size(); ++c)
    {
        for (size_t x = 0; x < pixelsPerLine; ++x)
        {
            float value = 0.0f;
            if (channels[c].pixelType == 1)
            {
                if (at + 2 > static_cast<size_t>(dataSize))
                    return false;
                const uint16_t half =
                    static_cast<uint16_t>(data[at]) | (static_cast<uint16_t>(data[at + 1]) << 8);
                value = halfToFloat(half);
                at += 2;
            }
            else
            {
                if (at + 4 > static_cast<size_t>(dataSize))
                    return false;
                value = readF32(data + at);
                at += 4;
            }
            line[x * channels.size() + c] = value;
        }
    }
    return true;
}

bool readExrChannels(const std::uint8_t *value, size_t size, std::vector<ExrChannel> &channels,
                     std::string &error)
{
    size_t at = 0;
    while (at < size)
    {
        const char *cname = reinterpret_cast<const char *>(value + at);
        size_t len = 0;
        while (at + len < size && cname[len] != '\0')
            ++len;
        if (len == 0)
            break;
        // channel struct after the name: pixelType(4) pLinear(1)
        // reserved(3) xSampling(4) ySampling(4) = 16 bytes.
        if (at + len + 1 + 16 > size)
            break;
        if (channels.size() >= cap::kMaxExrChannels)
        {
            error = "OpenEXR has too many channels";
            return false;
        }
        ExrChannel c;
        const size_t copy = len < sizeof(c.name) - 1 ? len : sizeof(c.name) - 1;
        std::memset(c.name, 0, sizeof(c.name));
        std::memcpy(c.name, cname, copy);
        at += len + 1;
        c.pixelType = readI32(value + at);
        at += 4; // pixelType
        at += 1; // pLinear
        at += 3; // reserved
        at += 4; // xSampling
        at += 4; // ySampling
        channels.push_back(c);
    }
    return true;
}

// Parse attributes. `pos` ends after the terminating NUL, before the offset table.
bool parseExrHeader(const std::vector<std::uint8_t> &file, size_t &pos, ExrHeader &out,
                    std::string &error)
{
    auto readString = [&](std::string &value) -> bool
    {
        if (pos >= file.size())
            return false;
        const size_t start = pos;
        while (pos < file.size() && file[pos] != 0)
            ++pos;
        if (pos >= file.size())
            return false;
        value.assign(reinterpret_cast<const char *>(&file[start]), pos - start);
        ++pos; // NUL
        return true;
    };

    while (pos < file.size())
    {
        std::string name;
        if (!readString(name))
        {
            error = "truncated OpenEXR header";
            return false;
        }
        if (name.empty())
            break; // end of header
        std::string type;
        if (!readString(type) || file.size() - pos < 4)
        {
            error = "truncated OpenEXR attribute";
            return false;
        }
        const int32_t size = readI32(&file[pos]);
        pos += 4;
        if (size < 0 || static_cast<std::uint64_t>(size) > file.size() - pos)
        {
            error = "invalid OpenEXR attribute size";
            return false;
        }
        const std::uint8_t *value = &file[pos];
        if (name == "channels")
        {
            if (!readExrChannels(value, static_cast<size_t>(size), out.channels, error))
                return false;
        }
        else if (name == "dataWindow")
        {
            if (size < 16)
            {
                error = "invalid OpenEXR dataWindow";
                return false;
            }
            out.minX = readI32(value);
            out.minY = readI32(value + 4);
            out.maxX = readI32(value + 8);
            out.maxY = readI32(value + 12);
        }
        else if (name == "compression" && size >= 1)
        {
            out.compression = value[0];
        }
        else if (name == "lineOrder" && size >= 1)
        {
            out.lineOrder = value[0];
        }
        pos += static_cast<size_t>(size);
    }
    return true;
}

// Widen both edges before the subtract-and-add, then narrow a capped span.
bool exrEdge(int lo, int hi, int &edge)
{
    const std::int64_t span =
        static_cast<std::int64_t>(hi) - static_cast<std::int64_t>(lo) + std::int64_t{1};
    if (span <= 0 || span > cap::kMaxHdrEdge)
        return false;
    edge = static_cast<int>(span);
    return true;
}

// Samples must fit before the RGB buffer exists. Overlapping offsets stay valid.
bool exrOutputBudget(const std::vector<std::uint8_t> &file,
                     const std::vector<std::uint64_t> &offsets,
                     const std::vector<ExrChannel> &channels, size_t pixelsPerLine,
                     std::string &error)
{
    const std::uint64_t lineFloats =
        static_cast<std::uint64_t>(pixelsPerLine) * static_cast<std::uint64_t>(channels.size());
    if (lineFloats > static_cast<std::uint64_t>(cap::kMaxHdrEdge) *
                         static_cast<std::uint64_t>(cap::kMaxExrChannels))
    {
        error = "OpenEXR scanline exceeds the allocation budget";
        return false;
    }
    std::uint64_t sampleBytes = 0;
    for (const ExrChannel &channel : channels)
        sampleBytes += channel.pixelType == 1 ? 2ull : 4ull;
    const std::uint64_t lineBytes = sampleBytes * pixelsPerLine;
    const std::uint64_t fileBytes = file.size();
    for (const std::uint64_t offset : offsets)
    {
        if (offset > fileBytes || fileBytes - offset < 8)
        {
            error = "truncated OpenEXR scanline";
            return false;
        }
        const int32_t dataSize = readI32(&file[static_cast<size_t>(offset) + 4]);
        if (dataSize < 0 || static_cast<std::uint64_t>(dataSize) < lineBytes ||
            static_cast<std::uint64_t>(dataSize) > fileBytes - offset - 8)
        {
            error = "invalid OpenEXR scanline size";
            return false;
        }
    }
    return true;
}

// Decode each stored scanline and scatter RGB into the row-major output.
bool readExrScanlines(const std::vector<std::uint8_t> &file,
                      const std::vector<std::uint64_t> &offsets,
                      const std::vector<ExrChannel> &channels, size_t pixelsPerLine, int height,
                      int lineOrder, int rIdx, int gIdx, int bIdx, std::vector<float> &line,
                      std::vector<float> &rgb, std::string &error)
{
    const std::uint64_t fileBytes = file.size();
    for (int y = 0; y < height; ++y)
    {
        const std::uint64_t offset = offsets[static_cast<size_t>(y)];
        // Compare the uint64 offset before narrowing. A 32-bit size_t cast would truncate it.
        if (offset > fileBytes || fileBytes - offset < 8)
        {
            error = "truncated OpenEXR scanline";
            return false;
        }
        const size_t blockOffset = static_cast<size_t>(offset);
        const std::uint8_t *block = &file[blockOffset];
        const int32_t dataSize = readI32(block + 4);
        const std::uint8_t *data = block + 8;
        if (dataSize < 0 || static_cast<std::uint64_t>(dataSize) > fileBytes - offset - 8)
        {
            error = "invalid OpenEXR scanline size";
            return false;
        }
        if (!decodeExrScanline(data, dataSize, channels, pixelsPerLine, line))
        {
            error = "truncated OpenEXR scanline data";
            return false;
        }
        const int targetRow = (lineOrder == 2) ? (height - 1 - y) : y;
        const size_t rowBase = static_cast<size_t>(targetRow) * pixelsPerLine * 3u;
        for (size_t x = 0; x < pixelsPerLine; ++x)
        {
            const size_t src = x * channels.size();
            rgb[rowBase + x * 3u + 0u] = line[src + static_cast<size_t>(rIdx)];
            rgb[rowBase + x * 3u + 1u] = line[src + static_cast<size_t>(gIdx)];
            rgb[rowBase + x * 3u + 2u] = line[src + static_cast<size_t>(bIdx)];
        }
    }
    return true;
}

bool pfmScaleOk(double scale, float &scaleFactor)
{
    if (!std::isfinite(scale) || scale == 0.0)
        return false;
    scaleFactor = static_cast<float>(std::fabs(scale));
    return std::isfinite(scaleFactor) && scaleFactor != 0.0f;
}

// PFM row 0 is the bottom row. One raw row is stored into that destination.
bool decodePfmRows(std::istream &in, std::vector<float> &rgb, int width, int height, int channels,
                   bool littleEndian, float scaleFactor, std::string &error)
{
    const size_t rowSamples = static_cast<size_t>(width) * static_cast<size_t>(channels);
    const std::uint64_t rowBytes = rowSamples * sizeof(float);
    const std::uint64_t need = rowBytes * static_cast<std::uint64_t>(height);
    const auto start = in.tellg();
    in.seekg(0, std::ios::end);
    const auto end = in.tellg();
    if (start < 0 || end < start || static_cast<std::uint64_t>(end - start) < need)
    {
        error = "truncated PFM pixel data";
        return false;
    }
    in.seekg(start);
    if (rowBytes > static_cast<std::uint64_t>((std::numeric_limits<size_t>::max)()))
    {
        error = "PFM row exceeds the allocation budget";
        return false;
    }
    std::vector<std::uint8_t> raw(static_cast<size_t>(rowBytes));
    const std::uint64_t rgbFloats =
        static_cast<std::uint64_t>(width) * static_cast<std::uint64_t>(height) * 3ull;
    rgb.assign(static_cast<size_t>(rgbFloats), 0.0f);
    const size_t rowFloats = static_cast<size_t>(width) * 3u;
    for (int y = 0; y < height; ++y)
    {
        in.read(reinterpret_cast<char *>(raw.data()), static_cast<std::streamsize>(raw.size()));
        if (static_cast<std::uint64_t>(in.gcount()) != rowBytes)
        {
            error = "truncated PFM pixel data";
            return false;
        }
        const size_t row = static_cast<size_t>(height - 1 - y) * rowFloats;
        for (int x = 0; x < width; ++x)
        {
            const size_t base = static_cast<size_t>(x) * static_cast<size_t>(channels) * 4u;
            if (channels == 1)
            {
                const float value = loadPfmSample(raw.data() + base, littleEndian, scaleFactor);
                // Grayscale PFM writes the same sample into R, G, and B.
                rgb[row + static_cast<size_t>(x) * 3u + 0u] = value;
                rgb[row + static_cast<size_t>(x) * 3u + 1u] = value;
                rgb[row + static_cast<size_t>(x) * 3u + 2u] = value;
            }
            else
            {
                for (int c = 0; c < 3; ++c)
                {
                    const size_t src = base + static_cast<size_t>(c) * 4u;
                    rgb[row + static_cast<size_t>(x) * 3u + static_cast<size_t>(c)] =
                        loadPfmSample(raw.data() + src, littleEndian, scaleFactor);
                }
            }
        }
    }
    return true;
}
} // namespace

bool HdrDecoder::canDecode(const std::string &path) const
{
    return hasExtension(path, "exr") || hasExtension(path, "pfm");
}

std::vector<std::string> HdrDecoder::extensions() const
{
    return {"exr", "pfm"};
}

ImageData HdrDecoder::toneMapToRgb24(const std::vector<float> &rgb, int width, int height,
                                     const ToneMap &tone)
{
    ImageData out = makeImageData(width, height, PixelFormat::RGB24);
    if (out.isNull() || static_cast<int>(rgb.size()) < width * height * 3)
        return ImageData{};
    const ImageBuffer view = out.view();
    const double invGamma = 1.0 / (tone.gamma > 0.05 ? tone.gamma : 2.2);
    const size_t count = static_cast<size_t>(width) * static_cast<size_t>(height) * 3u;
    for (size_t i = 0; i < count; ++i)
    {
        const double linear = static_cast<double>(rgb[i]) * tone.exposure;
        const double mapped = linear / (1.0 + linear);
        double srgb = std::pow(mapped > 0.0 ? mapped : 0.0, invGamma);
        if (!(srgb <= 1.0))
            srgb = 1.0;
        view.data[i] = static_cast<uint8_t>(srgb * 255.0 + 0.5);
    }
    return out;
}

bool HdrDecoder::readPfm(const std::string &path, std::vector<float> &rgb, int &width, int &height,
                         std::string &error) const
{
    std::ifstream in(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!in)
    {
        error = "cannot open file";
        return false;
    }
    std::string magic;
    if (!readHeaderLine(in, magic, error) || (magic != "PF" && magic != "Pf"))
    {
        if (error.empty())
            error = "not a PFM header";
        return false;
    }
    const bool color = (magic == "PF");
    std::string dims;
    if (!readHeaderLine(in, dims, error))
        return false;
    std::istringstream ds(dims);
    if (!(ds >> width >> height) || width <= 0 || height <= 0)
    {
        error = "invalid PFM dimensions";
        return false;
    }
    std::string scaleLine;
    if (!readHeaderLine(in, scaleLine, error))
        return false;
    std::istringstream ss(scaleLine);
    double scale = 0.0;
    float scaleFactor = 0.0f;
    if (!(ss >> scale) || !pfmScaleOk(scale, scaleFactor))
    {
        error = "invalid PFM scale";
        return false;
    }
    const bool littleEndian = scale < 0.0;
    const int channels = color ? 3 : 1;
    if (width > cap::kMaxHdrEdge || height > cap::kMaxHdrEdge ||
        static_cast<uint64_t>(width) * static_cast<uint64_t>(height) *
                static_cast<uint64_t>(channels) >
            cap::kMaxHdrPixels)
    {
        error = "PFM image is too large";
        return false;
    }
    return decodePfmRows(in, rgb, width, height, channels, littleEndian, scaleFactor, error);
}

bool HdrDecoder::readExr(const std::string &path, std::vector<float> &rgb, int &width, int &height,
                         std::string &error) const
{
    std::ifstream in(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!in)
    {
        error = "cannot open file";
        return false;
    }
    std::vector<std::uint8_t> file;
    if (!readBoundedExr(in, file, error))
        return false;
    if (!exrMagic(file))
    {
        error = "not an OpenEXR file";
        return false;
    }
    // Low byte is the version. Upper bytes are flags (tiled = 0x200).
    const uint32_t versionField = readU32(file.data() + 4);
    if (versionField & 0x200u)
    {
        error = "OpenEXR tiled files are not supported";
        return false;
    }
    size_t pos = 8;
    ExrHeader header;
    if (!parseExrHeader(file, pos, header, error))
        return false;
    const int compression = header.compression;
    const int lineOrder = header.lineOrder;
    std::vector<ExrChannel> channels = std::move(header.channels);

    if (compression != 0)
    {
        error = "only uncompressed OpenEXR is supported";
        return false;
    }
    if (!exrEdge(header.minX, header.maxX, width) || !exrEdge(header.minY, header.maxY, height) ||
        channels.empty())
    {
        error = "invalid OpenEXR data window";
        return false;
    }
    if (static_cast<uint64_t>(width) * static_cast<uint64_t>(height) > cap::kMaxHdrPixels)
    {
        error = "OpenEXR image is too large";
        return false;
    }
    if (channels.size() > cap::kMaxExrChannels)
    {
        error = "OpenEXR has too many channels";
        return false;
    }

    const size_t scanLines = static_cast<size_t>(height);
    if (pos > file.size() || (file.size() - pos) / 8 < scanLines)
    {
        error = "truncated OpenEXR offset table";
        return false;
    }
    std::vector<uint64_t> offsets(scanLines);
    for (size_t i = 0; i < scanLines; ++i)
        offsets[i] = readU64(&file[pos + i * 8]);
    pos += scanLines * 8;

    int rIdx = -1, gIdx = -1, bIdx = -1;
    for (size_t i = 0; i < channels.size(); ++i)
    {
        if (std::strcmp(channels[i].name, "R") == 0)
            rIdx = static_cast<int>(i);
        else if (std::strcmp(channels[i].name, "G") == 0)
            gIdx = static_cast<int>(i);
        else if (std::strcmp(channels[i].name, "B") == 0)
            bIdx = static_cast<int>(i);
    }
    if (rIdx < 0 || gIdx < 0 || bIdx < 0)
    {
        error = "OpenEXR has no RGB channels";
        return false;
    }

    const size_t pixelsPerLine = static_cast<size_t>(width);
    if (!exrOutputBudget(file, offsets, channels, pixelsPerLine, error))
        return false;
    std::vector<float> line(pixelsPerLine * channels.size());
    const std::uint64_t rgbFloats =
        static_cast<std::uint64_t>(pixelsPerLine) * static_cast<std::uint64_t>(height) * 3ull;
    rgb.assign(static_cast<size_t>(rgbFloats), 0.0f);
    return readExrScanlines(file, offsets, channels, pixelsPerLine, height, lineOrder, rIdx, gIdx,
                            bIdx, line, rgb, error);
}

bool HdrDecoder::decodeFile(const std::string &path, std::vector<float> &rgb, int &width,
                            int &height, bool &isFloat, std::string &error) const
{
    if (hasExtension(path, "pfm"))
    {
        isFloat = true;
        return readPfm(path, rgb, width, height, error);
    }
    if (hasExtension(path, "exr"))
    {
        isFloat = true;
        return readExr(path, rgb, width, height, error);
    }
    error = "unsupported HDR extension";
    return false;
}

ImageData HdrDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData HdrDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeScaled(path, maxEdge, meta);
}

ImageData HdrDecoder::decodeScaled(const std::string &path, int maxEdge,
                                   mviewer::domain::ImageMetadata &outMeta) const
{
    std::vector<float> rgb;
    int width = 0, height = 0;
    bool isFloat = false;
    std::string error;
    if (!decodeFile(path, rgb, width, height, isFloat, error))
    {
        fprintf(stderr, "HDR decode failed: %s\n", error.c_str());
        return ImageData{};
    }

    outMeta.width = width;
    outMeta.height = height;
    outMeta.bitDepth = isFloat ? 32 : 16;
    outMeta.channels = 3;
    outMeta.format = hasExtension(path, "exr") ? "EXR" : "PFM";
    outMeta.colorSpace = "linear";
    outMeta.textKeys["MViewer.Hdr"] = "1";

    if (maxEdge > 0 && (width > maxEdge || height > maxEdge))
    {
        const double scale =
            static_cast<double>(maxEdge) / static_cast<double>(std::max(width, height));
        const int dw = std::max(1, static_cast<int>(std::lround(width * scale)));
        const int dh = std::max(1, static_cast<int>(std::lround(height * scale)));
        std::vector<float> small(static_cast<size_t>(dw) * static_cast<size_t>(dh) * 3u, 0.0f);
        for (int y = 0; y < dh; ++y)
        {
            for (int x = 0; x < dw; ++x)
            {
                const int sx = std::min(width - 1, static_cast<int>(x / scale));
                const int sy = std::min(height - 1, static_cast<int>(y / scale));
                const size_t src = (static_cast<size_t>(sy) * static_cast<size_t>(width) +
                                    static_cast<size_t>(sx)) *
                                   3u;
                const size_t dst =
                    (static_cast<size_t>(y) * static_cast<size_t>(dw) + static_cast<size_t>(x)) *
                    3u;
                small[dst + 0u] = rgb[src + 0u];
                small[dst + 1u] = rgb[src + 1u];
                small[dst + 2u] = rgb[src + 2u];
            }
        }
        return toneMapToRgb24(small, dw, dh, ToneMap{});
    }
    return toneMapToRgb24(rgb, width, height, ToneMap{});
}

ImageData HdrDecoder::decodeFull(const std::string &path,
                                 mviewer::domain::ImageMetadata &outMeta) const
{
    std::vector<float> rgb;
    int width = 0, height = 0;
    bool isFloat = false;
    std::string error;
    if (!decodeFile(path, rgb, width, height, isFloat, error))
    {
        fprintf(stderr, "HDR decode failed: %s\n", error.c_str());
        return ImageData{};
    }

    outMeta.width = width;
    outMeta.height = height;
    outMeta.bitDepth = isFloat ? 32 : 16;
    outMeta.channels = 3;
    outMeta.format = hasExtension(path, "exr") ? "EXR" : "PFM";
    outMeta.colorSpace = "linear";
    outMeta.textKeys["MViewer.Hdr"] = "1";
    return toneMapToRgb24(rgb, width, height, ToneMap{});
}
