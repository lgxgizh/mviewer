// NOLINTBEGIN
// Strided previews of headerless .raw / .gray dumps.
//
// A 2x phase-preserving grid keeps one Bayer quad, or a 2x2 mono block, from
// each preview footprint. The full-frame preview averages every sample in that
// footprint. On the ramps below (linear encode, white balance off, 4 codes per
// pixel for 10-bit and 1 code per pixel for 8-bit) those averages stay within
// 8 levels per channel. A flat field must match the full-frame render exactly.
// maxEdge 0, a maxEdge that does not preview, and a shrink below 2 on both
// axes stay on the full payload path and must be byte-identical.

#include "core/image/rawplain/PlainRawFile.h"
#include "core/image/rawplain/PlainRawPreview.h"
#include "core/image/rawplain/PlainRawRender.h"

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <fstream>
#include <random>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

namespace
{

constexpr int kPreviewTolerance = 8;

using mviewer::core::rawplain::Cfa;
using mviewer::core::rawplain::decodePlainRawFile;
using mviewer::core::rawplain::fillPlainRawMetadata;
using mviewer::core::rawplain::grayDefaultParams;
using mviewer::core::rawplain::minRowBytes;
using mviewer::core::rawplain::Packing;
using mviewer::core::rawplain::Params;
using mviewer::core::rawplain::PlainRawFileInfo;
using mviewer::core::rawplain::plainRawParamKey;
using mviewer::core::rawplain::PlainRawParamStore;
using mviewer::core::rawplain::readPlainRawSamples;
using mviewer::core::rawplain::renderPlainRawOwned;
using mviewer::domain::ImageMetadata;

int maxChannelDelta(const ImageData &a, const ImageData &b)
{
    if (a.isNull() || b.isNull() || !a.buffer || !b.buffer || a.width != b.width ||
        a.height != b.height || a.format != b.format || a.buffer->size() != b.buffer->size())
    {
        return 1000;
    }
    int worst = 0;
    const std::vector<std::uint8_t> &left = *a.buffer;
    const std::vector<std::uint8_t> &right = *b.buffer;
    for (std::size_t i = 0; i < left.size(); ++i)
    {
        const int delta = left[i] > right[i] ? static_cast<int>(left[i] - right[i])
                                             : static_cast<int>(right[i] - left[i]);
        if (delta > worst)
        {
            worst = delta;
        }
    }
    return worst;
}

bool sameImage(const ImageData &a, const ImageData &b)
{
    return maxChannelDelta(a, b) == 0;
}

int spread(const ImageData &image)
{
    if (image.isNull() || !image.buffer || image.buffer->empty())
    {
        return 0;
    }
    int lo = (*image.buffer)[0];
    int hi = lo;
    for (std::uint8_t value : *image.buffer)
    {
        if (value < lo)
        {
            lo = value;
        }
        if (value > hi)
        {
            hi = value;
        }
    }
    return hi - lo;
}

ImageData referencePreview(const std::string &path, int maxEdge)
{
    PlainRawFileInfo info = mviewer::core::rawplain::resolvePlainRawFile(path);
    std::vector<std::uint16_t> samples;
    std::string why;
    if (!readPlainRawSamples(path, info, samples, &why))
    {
        return {};
    }
    return renderPlainRawOwned(info.resolved.params, std::move(samples), maxEdge);
}

void storeLsb16(std::uint8_t *row, const std::uint16_t *samples, int width)
{
    for (int x = 0; x < width; ++x)
    {
        const unsigned value = samples[x];
        const std::size_t at = static_cast<std::size_t>(x) * 2U;
        row[at] = static_cast<std::uint8_t>(value & 0xFFu);
        row[at + 1U] = static_cast<std::uint8_t>((value >> 8) & 0xFFu);
    }
}

void storeMipi10(std::uint8_t *row, const std::uint16_t *samples, int width)
{
    const int groups = (width + 3) / 4;
    for (int group = 0; group < groups; ++group)
    {
        const int x0 = group * 4;
        std::uint8_t *bytes = row + static_cast<std::size_t>(group) * 5U;
        unsigned lsb = 0;
        const int count = (width - x0 >= 4) ? 4 : (width - x0);
        for (int k = 0; k < 4; ++k)
        {
            bytes[k] = 0;
        }
        for (int k = 0; k < count; ++k)
        {
            const unsigned value = samples[x0 + k] & 0x3FFu;
            bytes[k] = static_cast<std::uint8_t>((value >> 2) & 0xFFu);
            lsb |= (value & 0x3u) << (2 * k);
        }
        bytes[4] = static_cast<std::uint8_t>(lsb);
    }
}

std::vector<std::uint8_t> packSamples(const Params &p, const std::vector<std::uint16_t> &samples)
{
    const std::int64_t stride = p.rowStride > 0 ? p.rowStride : minRowBytes(p);
    const std::int64_t total = p.headerOffset + stride * static_cast<std::int64_t>(p.height);
    std::vector<std::uint8_t> out(static_cast<std::size_t>(total), 0x5A);
    for (int y = 0; y < p.height; ++y)
    {
        std::uint8_t *row = out.data() + static_cast<std::size_t>(p.headerOffset + stride * y);
        const std::uint16_t *src =
            samples.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(p.width);
        if (p.packing == Packing::Bits8)
        {
            for (int x = 0; x < p.width; ++x)
            {
                row[x] = static_cast<std::uint8_t>(src[x]);
            }
        }
        else if (p.packing == Packing::Mipi10)
        {
            storeMipi10(row, src, p.width);
        }
        else
        {
            storeLsb16(row, src, p.width);
        }
    }
    return out;
}

void writeBytes(const std::filesystem::path &path, const std::vector<std::uint8_t> &bytes)
{
    std::ofstream out(path, std::ios::binary);
    out.write(reinterpret_cast<const char *>(bytes.data()),
              static_cast<std::streamsize>(bytes.size()));
}

std::vector<std::uint16_t> ramp(int width, int height, int bias, int slope, int cap)
{
    std::vector<std::uint16_t> samples(static_cast<std::size_t>(width) *
                                       static_cast<std::size_t>(height));
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < width; ++x)
        {
            int value = bias + (x + y) * slope;
            if (value > cap)
            {
                value = cap;
            }
            if (value < 0)
            {
                value = 0;
            }
            const std::size_t index =
                static_cast<std::size_t>(y) * static_cast<std::size_t>(width) +
                static_cast<std::size_t>(x);
            samples[index] = static_cast<std::uint16_t>(value);
        }
    }
    return samples;
}

Params sensor(int width, int height, Packing packing, int bits)
{
    Params params;
    params.width = width;
    params.height = height;
    params.bitDepth = bits;
    params.packing = packing;
    params.cfa = Cfa::RGGB;
    params.wb = mviewer::core::rawplain::WhiteBalance::None;
    params.srgbGamma = false;
    return params;
}

Params monoGray(int width, int height)
{
    Params params = grayDefaultParams();
    params.width = width;
    params.height = height;
    params.bitDepth = 8;
    params.packing = Packing::Bits8;
    params.cfa = Cfa::Mono;
    params.srgbGamma = false;
    return params;
}

std::string writeOverride(const std::filesystem::path &file, const Params &params,
                          const std::vector<std::uint16_t> &samples)
{
    writeBytes(file, packSamples(params, samples));
    const std::string path = file.string();
    PlainRawParamStore::instance().setOverride(plainRawParamKey(path), params);
    return path;
}

void checkIdentical(const std::string &path, int maxEdge)
{
    const ImageData got = decodePlainRawFile(path, maxEdge, nullptr, nullptr);
    const ImageData ref = referencePreview(path, maxEdge);
    if (!sameImage(got, ref))
    {
        std::printf("identity mismatch maxEdge %d delta %d\n", maxEdge, maxChannelDelta(got, ref));
    }
    CHECK(sameImage(got, ref));
}

void checkClose(const std::string &path, int maxEdge, int srcW, int srcH)
{
    int outW = 0;
    int outH = 0;
    CHECK(mviewer::core::rawplain::detail::previewDimensions(srcW, srcH, maxEdge, outW, outH));
    std::string why;
    PlainRawFileInfo info;
    const ImageData got = decodePlainRawFile(path, maxEdge, &info, &why);
    const ImageData ref = referencePreview(path, maxEdge);
    CHECK(why.empty());
    CHECK(!got.isNull());
    CHECK(!ref.isNull());
    CHECK(got.format == PixelFormat::RGB24);
    CHECK(got.width == outW);
    CHECK(got.height == outH);
    CHECK(info.resolved.params.width == srcW);
    CHECK(info.resolved.params.height == srcH);
    const int delta = maxChannelDelta(got, ref);
    if (delta > kPreviewTolerance)
    {
        std::printf("preview delta %d (tolerance %d) %dx%d edge %d\n", delta, kPreviewTolerance,
                    srcW, srcH, maxEdge);
    }
    CHECK(delta <= kPreviewTolerance);
    CHECK(spread(got) > kPreviewTolerance);
}

void testLsb16(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const int width = 64;
    const int height = 48;
    const Params params = sensor(width, height, Packing::Lsb16, 10);
    const std::string path =
        writeOverride(dir / "ramp_64x48.raw", params, ramp(width, height, 32, 4, 1023));
    std::vector<std::uint16_t> full;
    PlainRawFileInfo info = mviewer::core::rawplain::resolvePlainRawFile(path);
    std::string why;
    CHECK(readPlainRawSamples(path, info, full, &why));
    CHECK(static_cast<int>(full.size()) == width * height);
    checkClose(path, 16, width, height);
    checkIdentical(path, 0);
    checkIdentical(path, 100000);
    // 64/40 and 48/30 are both below 2, so the preview still reads every sample.
    checkIdentical(path, 40);

    PlainRawParamStore::instance().clearAll();
    const std::string flat =
        writeOverride(dir / "flat_64x48.raw", params, ramp(width, height, 400, 0, 1023));
    const ImageData got = decodePlainRawFile(flat, 16, nullptr, nullptr);
    const ImageData ref = referencePreview(flat, 16);
    CHECK(sameImage(got, ref));
    CHECK(got.width == 16);
    CHECK(got.height == 12);
}

void testBits8(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const int width = 64;
    const int height = 48;
    const Params params = sensor(width, height, Packing::Bits8, 8);
    const std::string path =
        writeOverride(dir / "bytes_64x48.raw", params, ramp(width, height, 16, 1, 255));
    checkClose(path, 16, width, height);
    checkIdentical(path, 0);
}

void testMipi10(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const int width = 64;
    const int height = 48;
    const Params params = sensor(width, height, Packing::Mipi10, 10);
    const std::string path =
        writeOverride(dir / "mipi_64x48.raw", params, ramp(width, height, 32, 4, 1023));
    checkClose(path, 16, width, height);
    checkIdentical(path, 0);
}

void testGray(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const int width = 64;
    const int height = 32;
    Params tight = monoGray(width, height);
    const std::string path =
        writeOverride(dir / "scan_64x32.gray", tight, ramp(width, height, 16, 1, 255));
    checkClose(path, 16, width, height);
    checkIdentical(path, 0);
    std::string why;
    PlainRawFileInfo info;
    const ImageData preview = decodePlainRawFile(path, 16, &info, &why);
    CHECK(!preview.isNull());
    ImageMetadata meta;
    fillPlainRawMetadata(info, path, meta);
    CHECK(meta.width == width);
    CHECK(meta.height == height);
    CHECK(meta.channels == 1);
    CHECK(meta.format == "Gray (headerless)");
    CHECK(meta.bitDepth == 8);

    Params padded = monoGray(width, height);
    padded.headerOffset = 16;
    padded.rowStride = 71;
    const std::string padPath =
        writeOverride(dir / "pad_64x32.gray", padded, ramp(width, height, 16, 1, 255));
    checkClose(padPath, 16, width, height);
    checkIdentical(padPath, 0);
    PlainRawFileInfo padInfo;
    const ImageData padPreview = decodePlainRawFile(padPath, 16, &padInfo, &why);
    CHECK(padPreview.width == 16);
    CHECK(padPreview.height == 8);
    CHECK(padInfo.resolved.params.width == width);
    CHECK(padInfo.resolved.params.headerOffset == 16);
    CHECK(padInfo.resolved.params.rowStride == 71);
    CHECK(padInfo.resolved.params.cfa == Cfa::Mono);
}

void testPaddedLsb16(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const int width = 64;
    const int height = 48;
    Params params = sensor(width, height, Packing::Lsb16, 10);
    params.headerOffset = 16;
    params.rowStride = 137;
    const std::string path =
        writeOverride(dir / "pad_64x48.raw", params, ramp(width, height, 32, 4, 1023));
    checkClose(path, 16, width, height);
    checkIdentical(path, 0);
    PlainRawFileInfo info;
    std::string why;
    const ImageData preview = decodePlainRawFile(path, 16, &info, &why);
    CHECK(info.resolved.params.width == width);
    CHECK(info.resolved.params.height == height);
    CHECK(info.resolved.params.headerOffset == 16);
    CHECK(info.resolved.params.rowStride == 137);
    CHECK(info.resolved.params.bitDepth == 10);
    CHECK(info.depthNote.empty());
    CHECK(preview.width == 16);
    CHECK(preview.height == 12);
}

void testExactTwoIsFullRead(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const Params params = sensor(80, 80, Packing::Lsb16, 10);
    const std::string path =
        writeOverride(dir / "even_80x80.raw", params, ramp(80, 80, 32, 4, 1023));
    // Shrink is exactly 2, so the 2x grid is the whole frame and must match
    // today's full render byte for byte.
    checkIdentical(path, 40);
    checkIdentical(path, 0);
}

void testDepthNotReprobed(const std::filesystem::path &dir)
{
    PlainRawParamStore::instance().clearAll();
    const int width = 64;
    const int height = 48;
    std::vector<std::uint16_t> samples = ramp(width, height, 80, 0, 1023);
    samples.back() = 2000;
    const auto file = dir / "depth_64x48.raw";
    Params layout = sensor(width, height, Packing::Lsb16, 10);
    writeBytes(file, packSamples(layout, samples));
    const std::string path = file.string();

    std::string why;
    PlainRawFileInfo fullInfo;
    const ImageData full = decodePlainRawFile(path, 0, &fullInfo, &why);
    CHECK(!full.isNull());
    CHECK(fullInfo.resolved.params.bitDepth == 12);
    CHECK(!fullInfo.depthNote.empty());

    PlainRawFileInfo previewInfo;
    const ImageData preview = decodePlainRawFile(path, 16, &previewInfo, &why);
    CHECK(!preview.isNull());
    CHECK(preview.width == 16);
    CHECK(preview.height == 12);
    CHECK(previewInfo.resolved.params.bitDepth == 10);
    CHECK(previewInfo.depthNote.empty());
    CHECK(previewInfo.resolved.params.width == width);
    CHECK(previewInfo.resolved.params.height == height);
    ImageMetadata meta;
    fillPlainRawMetadata(previewInfo, path, meta);
    CHECK(meta.width == width);
    CHECK(meta.height == height);
    CHECK(meta.bitDepth == 10);
    CHECK(meta.colorSpace == "RGGB 10-bit");
}

} // namespace

int main()
{
    PlainRawParamStore::instance().clearAll();
    std::mt19937 rng{std::random_device{}()};
    const auto dir = std::filesystem::temp_directory_path() /
                     ("mviewer_plainraw_stride_" + std::to_string(rng()));
    std::filesystem::create_directories(dir);

    testLsb16(dir);
    testBits8(dir);
    testMipi10(dir);
    testGray(dir);
    testPaddedLsb16(dir);
    testExactTwoIsFullRead(dir);
    testDepthNotReprobed(dir);

    std::error_code ec;
    std::filesystem::remove_all(dir, ec);
    PlainRawParamStore::instance().clearAll();
    if (g_failures != 0)
    {
        std::printf("test_plainraw_stride: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_plainraw_stride: all passed\n");
    return 0;
}
// NOLINTEND
