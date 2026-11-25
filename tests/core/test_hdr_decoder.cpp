// HDR decoder (report feature 4): PFM and uncompressed OpenEXR.
// Both are scene-referred linear float sources, so the checks focus on the
// tone map and on the container parsing rather than on pixel-exact colours.
#include "core/image/decoder/HdrDecoder.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/decoder/HdrDecoderInternal.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace fs = std::filesystem;

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
    } while (0)

void putU32(std::vector<std::uint8_t> &out, std::uint32_t v)
{
    out.push_back(static_cast<std::uint8_t>(v & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 8) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 16) & 0xFF));
    out.push_back(static_cast<std::uint8_t>((v >> 24) & 0xFF));
}

void putI32(std::vector<std::uint8_t> &out, std::int32_t v)
{
    putU32(out, static_cast<std::uint32_t>(v));
}

void putU64(std::vector<std::uint8_t> &out, std::uint64_t v)
{
    for (int i = 0; i < 8; ++i)
        out.push_back(static_cast<std::uint8_t>((v >> (8 * i)) & 0xFF));
}

void putF32(std::vector<std::uint8_t> &out, float v)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(v);
    putU32(out, bits);
}

void putString(std::vector<std::uint8_t> &out, const char *s)
{
    while (*s)
        out.push_back(static_cast<std::uint8_t>(*s++));
    out.push_back(0);
}

std::uint16_t floatToHalf(float value)
{
    const std::uint32_t bits = std::bit_cast<std::uint32_t>(value);
    const std::uint32_t sign = (bits >> 16) & 0x8000u;
    const std::uint32_t exp = ((bits >> 23) & 0xFFu) - 127 + 15;
    const std::uint32_t mant = (bits >> 13) & 0x3FFu;
    return static_cast<std::uint16_t>(sign | (exp << 10) | mant);
}

std::string writePfm(const std::string &path, int width, int height,
                     const std::vector<float> &rgbBottomUp)
{
    std::ofstream out(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!out)
        return "cannot open";
    out << "PF\n" << width << " " << height << "\n-1.0\n";
    out.write(reinterpret_cast<const char *>(rgbBottomUp.data()),
              static_cast<std::streamsize>(rgbBottomUp.size() * sizeof(float)));
    out.close();
    return {};
}

std::string writeExr(const std::string &path, int width, int height,
                     const std::vector<float> &rgb /* top-down, linear RGB */)
{
    std::vector<std::uint8_t> head;
    head.push_back(0x76);
    head.push_back(0x2f);
    head.push_back(0x31);
    head.push_back(0x01);
    putU32(head, 2); // version 2, no flags

    const auto attr =
        [&](const char *name, const char *type, const std::vector<std::uint8_t> &value)
    {
        putString(head, name);
        putString(head, type);
        putI32(head, static_cast<std::int32_t>(value.size()));
        head.insert(head.end(), value.begin(), value.end());
    };

    // channels: chlist, alphabetical (B, G, R) — the EXR requirement.
    std::vector<std::uint8_t> chlist;
    const auto channel = [&](const char *n)
    {
        putString(chlist, n);
        putI32(chlist, 1);   // 1 = HALF
        chlist.push_back(0); // pLinear (1 byte)
        chlist.push_back(0); // reserved[3]
        chlist.push_back(0);
        chlist.push_back(0);
        putI32(chlist, 1); // xSampling
        putI32(chlist, 1); // ySampling
    };
    channel("B");
    channel("G");
    channel("R");
    chlist.push_back(0);
    attr("channels", "chlist", chlist);

    std::vector<std::uint8_t> box;
    putI32(box, 0);
    putI32(box, 0);
    putI32(box, width - 1);
    putI32(box, height - 1);
    attr("dataWindow", "box2i", box);
    attr("displayWindow", "box2i", box);

    const std::vector<std::uint8_t> compression{0}; // 0 = NO_COMPRESSION
    attr("compression", "compression", compression);
    const std::vector<std::uint8_t> lineOrder{0}; // 0 = increasing Y
    attr("lineOrder", "lineOrder", lineOrder);
    std::vector<std::uint8_t> aspect;
    putF32(aspect, 1.0f);
    attr("pixelAspectRatio", "float", aspect);
    std::vector<std::uint8_t> center;
    putF32(center, 0.0f);
    putF32(center, 0.0f);
    attr("screenWindowCenter", "v2f", center);
    std::vector<std::uint8_t> swidth;
    putF32(swidth, 1.0f);
    attr("screenWindowWidth", "float", swidth);
    head.push_back(0); // end of header

    // Channel order in the file is alphabetical: B, G, R.
    const size_t dataPerLine = static_cast<size_t>(width) * 3u * 2u; // HALF = 2 bytes
    std::vector<std::uint8_t> body;
    std::vector<std::uint64_t> offsets;
    for (int y = 0; y < height; ++y)
    {
        offsets.push_back(static_cast<std::uint64_t>(head.size()) +
                          static_cast<std::uint64_t>(static_cast<size_t>(height) * 8u) +
                          static_cast<std::uint64_t>(body.size()));
        std::vector<std::uint8_t> block;
        putI32(block, y);
        putI32(block, static_cast<std::int32_t>(dataPerLine));
        const auto pushHalf = [&](float v)
        {
            const std::uint16_t h = floatToHalf(v);
            block.push_back(static_cast<std::uint8_t>(h & 0xFF));
            block.push_back(static_cast<std::uint8_t>((h >> 8) & 0xFF));
        };
        for (int x = 0; x < width; ++x)
        {
            const size_t src =
                (static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)) * 3u;
            pushHalf(rgb[src + 2u]); // B
            pushHalf(rgb[src + 1u]); // G
            pushHalf(rgb[src + 0u]); // R
        }
        body.insert(body.end(), block.begin(), block.end());
    }
    std::vector<std::uint8_t> offsetsBytes;
    for (std::uint64_t o : offsets)
        putU64(offsetsBytes, o);

    std::ofstream out(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!out)
        return "cannot open";
    out.write(reinterpret_cast<const char *>(head.data()),
              static_cast<std::streamsize>(head.size()));
    out.write(reinterpret_cast<const char *>(offsetsBytes.data()),
              static_cast<std::streamsize>(offsetsBytes.size()));
    out.write(reinterpret_cast<const char *>(body.data()),
              static_cast<std::streamsize>(body.size()));
    out.close();
    return {};
}

struct ExrFixture
{
    std::vector<std::uint8_t> bytes;
    size_t offsetAt = 0;
    size_t dataSizeAt = 0;
};

std::string channelName(int index)
{
    if (index == 0)
        return "B";
    if (index == 1)
        return "G";
    if (index == 2)
        return "R";
    std::string name = "c";
    name.push_back(static_cast<char>('0' + (index / 10) % 10));
    name.push_back(static_cast<char>('0' + index % 10));
    return name;
}

void appendChannel(std::vector<std::uint8_t> &chlist, const std::string &name)
{
    putString(chlist, name.c_str());
    putI32(chlist, 1);
    chlist.insert(chlist.end(), {0, 0, 0, 0});
    putI32(chlist, 1);
    putI32(chlist, 1);
}

void appendAttr(std::vector<std::uint8_t> &head, const char *name, const char *type,
                const std::vector<std::uint8_t> &value)
{
    putString(head, name);
    putString(head, type);
    putI32(head, static_cast<std::int32_t>(value.size()));
    head.insert(head.end(), value.begin(), value.end());
}

ExrFixture makeExr(int minX, int minY, int maxX, int maxY, int channelCount, bool withPixels)
{
    ExrFixture out;
    auto &bytes = out.bytes;
    bytes.insert(bytes.end(), {0x76, 0x2f, 0x31, 0x01});
    putU32(bytes, 2);
    std::vector<std::uint8_t> chlist;
    for (int i = 0; i < channelCount; ++i)
        appendChannel(chlist, channelName(i));
    chlist.push_back(0);
    appendAttr(bytes, "channels", "chlist", chlist);
    std::vector<std::uint8_t> box;
    putI32(box, minX);
    putI32(box, minY);
    putI32(box, maxX);
    putI32(box, maxY);
    appendAttr(bytes, "dataWindow", "box2i", box);
    appendAttr(bytes, "compression", "compression", {0});
    appendAttr(bytes, "lineOrder", "lineOrder", {0});
    bytes.push_back(0);

    const std::int64_t spanW =
        static_cast<std::int64_t>(maxX) - static_cast<std::int64_t>(minX) + 1;
    const std::int64_t spanH =
        static_cast<std::int64_t>(maxY) - static_cast<std::int64_t>(minY) + 1;
    if (!withPixels || spanW <= 0 || spanH <= 0 || spanW > 8 || spanH > 8)
        return out;
    const auto width = static_cast<size_t>(spanW);
    const auto height = static_cast<size_t>(spanH);
    const auto dataPerLine = width * static_cast<size_t>(channelCount) * 2u;
    const size_t headerEnd = bytes.size();
    out.offsetAt = headerEnd;
    const size_t table = height * 8u;
    out.dataSizeAt = headerEnd + table + 4u;
    std::vector<std::uint8_t> body;
    for (size_t y = 0; y < height; ++y)
    {
        const auto at = headerEnd + table + body.size();
        putU64(bytes, static_cast<std::uint64_t>(at));
        putI32(body, static_cast<std::int32_t>(y));
        putI32(body, static_cast<std::int32_t>(dataPerLine));
        for (size_t x = 0; x < width; ++x)
        {
            for (int c = 0; c < channelCount; ++c)
            {
                const auto half = floatToHalf(c < 3 ? 0.18f : 0.0f);
                body.push_back(static_cast<std::uint8_t>(half & 0xFF));
                body.push_back(static_cast<std::uint8_t>((half >> 8) & 0xFF));
            }
        }
    }
    bytes.insert(bytes.end(), body.begin(), body.end());
    return out;
}

bool writeBytes(const fs::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(mviewer::core::pathFromUtf8(path.string()), std::ios::binary);
    if (!out)
        return false;
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
    return static_cast<bool>(out);
}

void patchU32(std::vector<std::uint8_t> &bytes, size_t at, std::uint32_t value)
{
    for (int i = 0; i < 4; ++i)
        bytes.at(at + static_cast<size_t>(i)) =
            static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
}

void patchU64(std::vector<std::uint8_t> &bytes, size_t at, std::uint64_t value)
{
    for (int i = 0; i < 8; ++i)
        bytes.at(at + static_cast<size_t>(i)) =
            static_cast<std::uint8_t>((value >> (8 * i)) & 0xFF);
}

void expectNull(HdrDecoder &decoder, const fs::path &path, const char *msg)
{
    mviewer::domain::ImageMetadata meta;
    const ImageData image = decoder.decodeFull(path.string(), meta);
    CHECK(image.isNull(), msg);
}

bool sameTone(const ImageData &got, const std::vector<float> &linear, int width, int height)
{
    const ImageData want = HdrDecoder::toneMapToRgb24(linear, width, height, {});
    if (got.isNull() || want.isNull() || got.width != width || got.height != height)
        return false;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            const PixelRGBA a = samplePixel(got, x, y);
            const PixelRGBA b = samplePixel(want, x, y);
            if (!a.valid || !b.valid || a.r != b.r || a.g != b.g || a.b != b.b)
                return false;
        }
    }
    return true;
}

std::vector<std::uint8_t> floatBytes(float value, bool littleEndian)
{
    std::vector<std::uint8_t> bytes;
    putF32(bytes, value);
    if (!littleEndian)
    {
        std::swap(bytes[0], bytes[3]);
        std::swap(bytes[1], bytes[2]);
    }
    return bytes;
}

bool writeText(const fs::path &path, const std::string &text)
{
    std::ofstream out(mviewer::core::pathFromUtf8(path.string()), std::ios::binary);
    if (!out)
        return false;
    out << text;
    return static_cast<bool>(out);
}

void checkPfmValues(HdrDecoder &decoder, const fs::path &root)
{
    const auto grayPath = root / "gray-le.pfm";
    std::vector<std::uint8_t> gray;
    const auto bottom = floatBytes(0.0f, true);
    const auto top = floatBytes(1.0f, true);
    gray.insert(gray.end(), bottom.begin(), bottom.end());
    gray.insert(gray.end(), top.begin(), top.end());
    CHECK(writeText(grayPath, "Pf\n1 2\n-1.0\n"), "grayscale PFM header written");
    {
        std::ofstream out(mviewer::core::pathFromUtf8(grayPath.string()),
                          std::ios::binary | std::ios::app);
        out.write(reinterpret_cast<const char *>(gray.data()),
                  static_cast<std::streamsize>(gray.size()));
    }
    mviewer::domain::ImageMetadata meta;
    const ImageData grayImage = decoder.decodeFull(grayPath.string(), meta);
    const std::vector<float> grayLinear = {1.0f, 1.0f, 1.0f, 0.0f, 0.0f, 0.0f};
    CHECK(sameTone(grayImage, grayLinear, 1, 2),
          "little-endian grayscale PFM keeps R, G, and B and flips rows");

    const auto rgbPath = root / "rgb-be.pfm";
    CHECK(writeText(rgbPath, "PF\n1 1\n1.0\n"), "RGB PFM header written");
    const float channels[3] = {0.0f, 0.5f, 1.0f};
    {
        std::ofstream out(mviewer::core::pathFromUtf8(rgbPath.string()),
                          std::ios::binary | std::ios::app);
        for (float value : channels)
        {
            const auto be = floatBytes(value, false);
            out.write(reinterpret_cast<const char *>(be.data()), 4);
        }
    }
    const ImageData rgbImage = decoder.decodeFull(rgbPath.string(), meta);
    const std::vector<float> rgbLinear = {0.0f, 0.5f, 1.0f};
    CHECK(sameTone(rgbImage, rgbLinear, 1, 1), "big-endian RGB PFM keeps each channel");

    const auto scaledPath = root / "scaled-le.pfm";
    CHECK(writeText(scaledPath, "Pf\n1 1\n-2.0\n"), "scaled PFM header written");
    {
        std::ofstream out(mviewer::core::pathFromUtf8(scaledPath.string()),
                          std::ios::binary | std::ios::app);
        const auto sample = floatBytes(0.25f, true);
        out.write(reinterpret_cast<const char *>(sample.data()), 4);
    }
    const ImageData scaled = decoder.decodeFull(scaledPath.string(), meta);
    const std::vector<float> scaledLinear = {0.5f, 0.5f, 0.5f};
    CHECK(sameTone(scaled, scaledLinear, 1, 1), "negative PFM scale keeps magnitude and endian");
}

void checkPfmRejects(HdrDecoder &decoder, const fs::path &root)
{
    const char *scales[] = {"0", "-0", "nan", "inf", "-inf"};
    for (const char *scale : scales)
    {
        const auto path = root / (std::string("bad-") + scale + ".pfm");
        CHECK(writeText(path, std::string("PF\n1 1\n") + scale + "\n"),
              "PFM scale fixture written");
        expectNull(decoder, path, "non-finite or zero PFM scale is rejected");
    }
    const auto longPath = root / "long-line.pfm";
    CHECK(
        writeText(longPath, std::string("PF\n") +
                                std::string(mviewer::core::hdr_detail::kMaxPfmHeaderLine + 1, '9') +
                                "\n1.0\n"),
        "overlong PFM header fixture written");
    expectNull(decoder, longPath, "overlong PFM header line is rejected");

    std::string cappedScale(mviewer::core::hdr_detail::kMaxPfmHeaderLine, ' ');
    cappedScale[0] = '1';
    cappedScale[1] = '.';
    cappedScale[2] = '0';
    const auto capPath = root / "scale-cap.pfm";
    CHECK(writeText(capPath, std::string("PF\n1 1\n") + cappedScale + "\n"),
          "exact PFM header cap fixture written");
    {
        std::ofstream out(mviewer::core::pathFromUtf8(capPath.string()),
                          std::ios::binary | std::ios::app);
        for (int i = 0; i < 3; ++i)
        {
            const auto sample = floatBytes(0.0f, false);
            out.write(reinterpret_cast<const char *>(sample.data()), 4);
        }
    }
    mviewer::domain::ImageMetadata meta;
    const ImageData capped = decoder.decodeFull(capPath.string(), meta);
    CHECK(!capped.isNull() && capped.width == 1 && capped.height == 1,
          "a PFM header line at the cap is accepted");
}

void checkExrBounds(HdrDecoder &decoder, const fs::path &root)
{
    const auto control = makeExr(0, 0, 0, 0, 3, true);
    const auto controlPath = root / "control.exr";
    CHECK(writeBytes(controlPath, control.bytes), "control EXR written");
    mviewer::domain::ImageMetadata meta;
    const ImageData controlImage = decoder.decodeFull(controlPath.string(), meta);
    CHECK(!controlImage.isNull() && controlImage.width == 1 && controlImage.height == 1,
          "small uncompressed EXR still decodes");
    const PixelRGBA px = samplePixel(controlImage, 0, 0);
    CHECK(px.valid && px.r > 80 && px.r < 140 && px.r == px.g && px.g == px.b,
          "EXR half samples stay neutral gray");

    const auto wide = makeExr(-1, 4, 0, 4, 3, true);
    const auto widePath = root / "negative-origin.exr";
    CHECK(writeBytes(widePath, wide.bytes), "negative-origin EXR written");
    const ImageData wideImage = decoder.decodeFull(widePath.string(), meta);
    CHECK(!wideImage.isNull() && wideImage.width == 2 && wideImage.height == 1,
          "dataWindow is widened before the span is accepted");

    const auto overflow = makeExr(0, 0, (std::numeric_limits<std::int32_t>::max)(), 0, 3, false);
    CHECK(writeBytes(root / "int-max-window.exr", overflow.bytes), "INT_MAX window written");
    expectNull(decoder, root / "int-max-window.exr",
               "INT_MAX dataWindow is rejected without int UB");

    const auto underflow = makeExr((std::numeric_limits<std::int32_t>::min)(), 0, -1, 0, 3, false);
    CHECK(writeBytes(root / "int-min-window.exr", underflow.bytes), "INT_MIN window written");
    expectNull(decoder, root / "int-min-window.exr",
               "INT_MIN dataWindow is rejected without int UB");

    const int limit = static_cast<int>(mviewer::core::hdr_detail::kMaxExrChannels);
    const auto atLimit = makeExr(0, 0, 0, 0, limit, true);
    CHECK(writeBytes(root / "channels-64.exr", atLimit.bytes), "channel ceiling EXR written");
    const ImageData limited = decoder.decodeFull((root / "channels-64.exr").string(), meta);
    CHECK(!limited.isNull() && limited.width == 1, "channel ceiling still decodes a 1x1 RGB EXR");

    const auto tooMany = makeExr(0, 0, 0, 0, limit + 1, false);
    CHECK(writeBytes(root / "channels-65.exr", tooMany.bytes), "channel overflow EXR written");
    expectNull(decoder, root / "channels-65.exr", "channel list above the ceiling is rejected");

    auto hugeOffset = control;
    patchU64(hugeOffset.bytes, hugeOffset.offsetAt, 0x100000008ull);
    CHECK(writeBytes(root / "offset-u64.exr", hugeOffset.bytes), "high offset EXR written");
    expectNull(decoder, root / "offset-u64.exr",
               "64-bit scanline offset is rejected before size_t narrowing");

    auto hugeSize = control;
    patchU32(hugeSize.bytes, hugeSize.dataSizeAt, 0x7fffffffu);
    CHECK(writeBytes(root / "datasize.exr", hugeSize.bytes), "huge dataSize EXR written");
    expectNull(decoder, root / "datasize.exr", "scanline dataSize above the buffer is rejected");

    auto truncated = control.bytes;
    truncated.pop_back();
    CHECK(writeBytes(root / "truncated.exr", truncated), "truncated EXR written");
    expectNull(decoder, root / "truncated.exr", "truncated EXR is rejected");
}

void checkExrInputCap(HdrDecoder &decoder, const fs::path &root)
{
    const auto path = root / "oversize.exr";
    {
        std::ofstream file(mviewer::core::pathFromUtf8(path.string()), std::ios::binary);
        file.put('x');
    }
    std::error_code ec;
    fs::resize_file(path, mviewer::core::hdr_detail::kMaxExrInputBytes + 1, ec);
    CHECK(!ec, "sparse oversized EXR fixture sized");
    expectNull(decoder, path, "EXR above the input cap is rejected before the payload is read");
    fs::remove(path, ec);
}
} // namespace

int main()
{
    const fs::path root = fs::temp_directory_path() / "mviewer_hdr_decoder_test";
    std::error_code ec;
    fs::remove_all(root, ec);
    fs::create_directories(root, ec);
    CHECK(!ec, "temp dir created");

    HdrDecoder decoder;

    // ── PFM ────────────────────────────────────────────────────────────────
    const int pw = 4, ph = 3;
    std::vector<float> pfm(static_cast<size_t>(pw) * ph * 3u, 0.0f);
    // Bottom-up rows: row 0 is the bottom of the image.
    const float rows[3][4] = {
        {0.0f, 0.18f, 1.0f, 4.0f}, {0.5f, 0.5f, 0.5f, 0.5f}, {1.0f, 1.0f, 1.0f, 1.0f}};
    for (int y = 0; y < ph; ++y)
        for (int x = 0; x < pw; ++x)
        {
            const size_t i = (static_cast<size_t>(y) * pw + x) * 3u;
            pfm[i + 0u] = rows[y][x];
            pfm[i + 1u] = rows[y][x];
            pfm[i + 2u] = rows[y][x];
        }
    const std::string pfmPath = (root / "gray.pfm").string();
    CHECK(writePfm(pfmPath, pw, ph, pfm).empty(), "PFM fixture written");

    mviewer::domain::ImageMetadata meta;
    const ImageData pfmImage = decoder.decodeFull(pfmPath, meta);
    CHECK(!pfmImage.isNull() && pfmImage.width == pw && pfmImage.height == ph,
          "PFM decodes to the stored size");
    CHECK(meta.format == "PFM" && meta.colorSpace == "linear", "PFM metadata says linear HDR");
    CHECK(meta.textKeys.count("MViewer.Hdr") == 1, "PFM is flagged as HDR");
    CHECK(decoder.canDecode(pfmPath) && decoder.extensions().size() == 2,
          "the decoder claims both HDR extensions");

    const PixelRGBA top = samplePixel(pfmImage, 0, 0);      // top row was value 1.0
    const PixelRGBA mid = samplePixel(pfmImage, 1, ph - 1); // bottom row, x=1 -> 0.18
    CHECK(top.valid && mid.valid, "PFM samples are readable");
    // Reinhard(1.0) = 0.5 -> gamma 2.2 -> ~0.73 -> ~186
    CHECK(top.r > 170 && top.r == top.g && top.g == top.b,
          "a 1.0 linear value tone-maps to a bright neutral pixel");
    // Reinhard(0.18) = 0.1525 -> ~0.42 -> ~108
    CHECK(mid.r > 80 && mid.r < 140 && mid.r == mid.g && mid.g == mid.b,
          "a 0.18 linear value maps to mid-gray on every channel");

    // PFM rows run bottom-to-top, so the bottom-left sample must be row 0, x=0.
    const PixelRGBA bottom = samplePixel(pfmImage, 0, ph - 1);
    CHECK(bottom.valid && bottom.r < 10, "PFM row order is flipped to top-down");

    const ImageData scaled = decoder.decodeScaled(pfmPath, 2);
    CHECK(!scaled.isNull() && std::max(scaled.width, scaled.height) == 2,
          "scaled HDR decode clamps the long edge");

    // ── OpenEXR (uncompressed scanline, HALF) ──────────────────────────────
    const int ew = 3, eh = 2;
    std::vector<float> exr(static_cast<size_t>(ew) * eh * 3u, 0.0f);
    for (size_t i = 0; i < exr.size(); i += 3)
    {
        exr[i + 0u] = 0.18f;
        exr[i + 1u] = 0.18f;
        exr[i + 2u] = 0.18f;
    }
    const std::string exrPath = (root / "gray.exr").string();
    CHECK(writeExr(exrPath, ew, eh, exr).empty(), "EXR fixture written");
    const ImageData exrImage = decoder.decodeFull(exrPath, meta);
    CHECK(!exrImage.isNull() && exrImage.width == ew && exrImage.height == eh,
          "uncompressed EXR decodes to the stored size");
    CHECK(meta.format == "EXR", "EXR metadata records the format");
    const PixelRGBA exrPx = samplePixel(exrImage, 1, 1);
    CHECK(exrPx.valid && exrPx.r > 80 && exrPx.r < 140 && exrPx.r == exrPx.g && exrPx.g == exrPx.b,
          "EXR half samples tone-map to mid-gray");

    // ── Tone map contract ──────────────────────────────────────────────────
    const std::vector<float> flat = {0.0f, 1.0f, 0.0f, 1.0f, 0.0f, 0.0f};
    const ImageData mapped = HdrDecoder::toneMapToRgb24(flat, 2, 1, {1.0, 2.2});
    CHECK(!mapped.isNull() && mapped.width == 2 && mapped.height == 1,
          "toneMapToRgb24 produces an RGB24 buffer");
    const PixelRGBA black = samplePixel(mapped, 0, 0);
    const PixelRGBA white = samplePixel(mapped, 1, 0);
    CHECK(black.valid && black.r == 0, "0.0 linear maps to black");
    CHECK(white.valid && white.r > 170, "1.0 linear maps to bright, not clipped to 255");
    CHECK(!decoder.canDecode((root / "nope.png").string()),
          "the decoder does not claim ordinary images");

    checkPfmValues(decoder, root);
    checkPfmRejects(decoder, root);
    checkExrBounds(decoder, root);
    checkExrInputCap(decoder, root);

    fs::remove_all(root, ec);
    printf("\n==== HDR decoder test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
