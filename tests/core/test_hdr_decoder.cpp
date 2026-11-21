// HDR decoder (report feature 4): PFM and uncompressed OpenEXR.
// Both are scene-referred linear float sources, so the checks focus on the
// tone map and on the container parsing rather than on pixel-exact colours.
#include "core/image/decoder/HdrDecoder.h"

#include "core/filesystem/Utf8Path.h"

#include <bit>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
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
    const size_t blockSize = static_cast<size_t>(sizeof(std::int32_t)) * 2u + dataPerLine;
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
    CHECK(top.r > 170, "a 1.0 linear value tone-maps to a bright pixel");
    // Reinhard(0.18) = 0.1525 -> ~0.42 -> ~108
    CHECK(mid.r > 80 && mid.r < 140, "a 0.18 linear value maps to mid-gray, not clipped");

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
    CHECK(exrPx.valid && exrPx.r > 80 && exrPx.r < 140, "EXR half samples tone-map to mid-gray");

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

    fs::remove_all(root, ec);
    printf("\n==== HDR decoder test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
