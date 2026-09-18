#include "core/compare/DifferenceEngine.h"
#include "core/simd/CpuFeatures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
#define MV_TARGET_AVX2 __attribute__((target("avx2")))
#define MV_TARGET_SSSE3 __attribute__((target("ssse3,sse4.1")))
#else
#define MV_TARGET_AVX2
#define MV_TARGET_SSSE3
#endif

namespace
{
struct HeatRGB
{
    uint8_t r, g, b;
};
static_assert(sizeof(HeatRGB) == 3, "HeatRGB must be packed 3 bytes");

const auto &heatLUT()
{
    static const auto table = []()
    {
        std::array<HeatRGB, 256> lut{};
        for (int v = 0; v < 256; ++v)
        {
            if (v < 128)
            {
                lut[v].r = 0;
                lut[v].g = static_cast<uint8_t>(v * 2);
                lut[v].b = static_cast<uint8_t>(255 - v * 2);
            }
            else
            {
                lut[v].r = static_cast<uint8_t>((v - 128) * 2);
                lut[v].g = static_cast<uint8_t>(255 - (v - 128) * 2);
                lut[v].b = 0;
            }
        }
        return lut;
    }();
    return table;
}

const auto &highlightIntensityLUT()
{
    static const auto table = []()
    {
        std::array<uint8_t, 256> lut{};
        for (int v = 0; v < 256; ++v)
            lut[v] = static_cast<uint8_t>(std::min(255, 80 + v * 2));
        return lut;
    }();
    return table;
}
} // namespace

ImageData DifferenceEngine::heatMap(const ImageData &gray)
{
    if (gray.isNull())
        return ImageData();
    const int w = gray.width;
    const int h = gray.height;
    const int cpp = gray.channelsPerPixel();
    const int ro = channelOffset(gray.format, 0);
    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();

    const auto &lut = heatLUT();
    if (cpp == 1 && ro == 0)
    {
        if (gray.stride() == static_cast<size_t>(w) && out.stride() == static_cast<size_t>(w) * 3)
        {
            const size_t total = static_cast<size_t>(w) * h;
            const uint8_t *src = gray.buffer->data();
            auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data());
            for (size_t i = 0; i < total; ++i)
                dst[i] = lut[src[i]];
        }
        else
        {
            for (int y = 0; y < h; ++y)
            {
                const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
                auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data() +
                                                        static_cast<size_t>(y) * out.stride());
                for (int x = 0; x < w; ++x)
                    dst[x] = lut[src[x]];
            }
        }
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride() + ro;
            auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data() +
                                                    static_cast<size_t>(y) * out.stride());
            for (int x = 0; x < w; ++x, src += cpp)
                dst[x] = lut[*src];
        }
    }
    return out;
}

namespace
{

void writeHighlightPixel(uint8_t *dst, uint8_t v, int minDiff, const uint8_t *intLUT, uint8_t grayR,
                         uint8_t grayG, uint8_t grayB)
{
    if (v >= static_cast<uint8_t>(minDiff))
    {
        dst[0] = intLUT[v];
        dst[1] = 0;
        dst[2] = 0;
    }
    else
    {
        dst[0] = grayR;
        dst[1] = grayG;
        dst[2] = grayB;
    }
}

void highlightContiguousNoBase(uint8_t *dst, const uint8_t *src, size_t total, int minDiff,
                               const uint8_t *intLUT)
{
    for (size_t i = 0; i < total; ++i, dst += 3)
        writeHighlightPixel(dst, src[i], minDiff, intLUT, 128, 128, 128);
}

void highlightRowsNoBase(ImageData &out, const ImageData &grayDiff, int w, int h, int cppD, int roD,
                         bool isDirectDiff, int minDiff, const uint8_t *intLUT)
{
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride() +
                             (isDirectDiff ? 0 : static_cast<size_t>(roD));
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < w; ++x, dst += 3, src += cppD)
            writeHighlightPixel(dst, *src, minDiff, intLUT, 128, 128, 128);
    }
}

void highlightContiguousGrayBase(uint8_t *dst, const uint8_t *src, const uint8_t *bs, size_t total,
                                 int minDiff, const uint8_t *intLUT)
{
    for (size_t i = 0; i < total; ++i, dst += 3)
        writeHighlightPixel(dst, src[i], minDiff, intLUT, bs[i], bs[i], bs[i]);
}

void highlightRowsGrayBase(ImageData &out, const ImageData &grayDiff, const ImageData &base, int w,
                           int h, int cppD, int roD, bool isDirectDiff, int minDiff,
                           const uint8_t *intLUT)
{
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride() +
                             (isDirectDiff ? 0 : static_cast<size_t>(roD));
        const uint8_t *bs = base.buffer->data() + static_cast<size_t>(y) * base.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < w; ++x, dst += 3, src += cppD, ++bs)
            writeHighlightPixel(dst, *src, minDiff, intLUT, *bs, *bs, *bs);
    }
}

void highlightRowsColorBase(ImageData &out, const ImageData &grayDiff, const ImageData &base, int w,
                            int h, int cppD, int cppB, int roD, bool isDirectDiff, bool isBGR,
                            int minDiff, const uint8_t *intLUT)
{
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride() +
                             (isDirectDiff ? 0 : static_cast<size_t>(roD));
        const uint8_t *bp = base.buffer->data() + static_cast<size_t>(y) * base.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < w; ++x, dst += 3, src += cppD, bp += cppB)
        {
            const int r = isBGR ? bp[2] : bp[0];
            const int g = bp[1];
            const int b = isBGR ? bp[0] : bp[2];
            const uint8_t gray = static_cast<uint8_t>((r * 77 + g * 151 + b * 28) >> 8);
            writeHighlightPixel(dst, *src, minDiff, intLUT, gray, gray, gray);
        }
    }
}

} // namespace

ImageData DifferenceEngine::highlightMap(const ImageData &grayDiff, const ImageData &base,
                                         uint8_t threshold)
{
    if (grayDiff.isNull())
        return ImageData();
    const int w = grayDiff.width;
    const int h = grayDiff.height;
    const int cppD = grayDiff.channelsPerPixel();
    const int roD = channelOffset(grayDiff.format, 0);

    const bool hasBase =
        !base.isNull() && base.width >= w && base.height >= h &&
        (base.format == PixelFormat::RGB24 || base.format == PixelFormat::BGR24 ||
         base.format == PixelFormat::RGBA32 || base.format == PixelFormat::BGRA32 ||
         base.format == PixelFormat::Grayscale8);
    const int cppB = hasBase ? base.channelsPerPixel() : 0;
    const int minDiff = std::max<int>(threshold, 1);

    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();

    const auto &intLUT = highlightIntensityLUT();
    const bool isDirectDiff = (cppD == 1 && roD == 0);

    if (!hasBase)
    {
        if (isDirectDiff && grayDiff.stride() == static_cast<size_t>(w) &&
            out.stride() == static_cast<size_t>(w) * 3)
        {
            highlightContiguousNoBase(out.buffer->data(), grayDiff.buffer->data(),
                                      static_cast<size_t>(w) * static_cast<size_t>(h), minDiff,
                                      intLUT.data());
            return out;
        }
        highlightRowsNoBase(out, grayDiff, w, h, cppD, roD, isDirectDiff, minDiff, intLUT.data());
        return out;
    }

    if (base.format == PixelFormat::Grayscale8)
    {
        if (isDirectDiff && grayDiff.stride() == static_cast<size_t>(w) &&
            base.stride() == static_cast<size_t>(w) && out.stride() == static_cast<size_t>(w) * 3)
        {
            highlightContiguousGrayBase(
                out.buffer->data(), grayDiff.buffer->data(), base.buffer->data(),
                static_cast<size_t>(w) * static_cast<size_t>(h), minDiff, intLUT.data());
            return out;
        }
        highlightRowsGrayBase(out, grayDiff, base, w, h, cppD, roD, isDirectDiff, minDiff,
                              intLUT.data());
        return out;
    }

    const bool isBGR = (base.format == PixelFormat::BGR24 || base.format == PixelFormat::BGRA32);
    highlightRowsColorBase(out, grayDiff, base, w, h, cppD, cppB, roD, isDirectDiff, isBGR, minDiff,
                           intLUT.data());
    return out;
}
