#include "core/compare/DifferenceEngine.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

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
            const uint8_t r = (v < 128) ? 0 : static_cast<uint8_t>((v - 128) * 2);
            const uint8_t g =
                (v < 128) ? static_cast<uint8_t>(v * 2) : static_cast<uint8_t>(255 - (v - 128) * 2);
            const uint8_t b = (v < 128) ? static_cast<uint8_t>(255 - v * 2) : 0;
            lut[v] = HeatRGB{r, g, b};
        }
        return lut;
    }();
    return table;
}

const auto &highlightColorLUT()
{
    static const auto table = []()
    {
        std::array<std::array<uint8_t, 3>, 256> lut{};
        for (int v = 0; v < 256; ++v)
        {
            const uint8_t r = static_cast<uint8_t>(std::min(255, 80 + v * 2));
            uint8_t g = 0;
            uint8_t b = 0;
            if (v < 60)
            {
                // Subtle difference: warm amber warning gradient, strictly R > G > B
                g = static_cast<uint8_t>(std::min<int>(r / 3, 48 - (v * 48 / 60)));
            }
            else if (v >= 180)
            {
                // Severe difference: vivid crimson alert
                b = static_cast<uint8_t>(std::min<int>(24, (v - 180) / 4));
            }
            lut[v] = {r, g, b};
        }
        return lut;
    }();
    return table;
}
} // namespace

ImageData DifferenceEngine::heatMap(const ImageData &gray)
{
    return visualizeOverlay(gray, ImageData(), 0, 1.0, false);
}

ImageData DifferenceEngine::highlightMap(const ImageData &grayDiff, const ImageData &base,
                                         uint8_t threshold)
{
    return visualizeOverlay(grayDiff, base, threshold, 1.0, true);
}

static ImageData renderHeatOverlay(const ImageData &grayDiff, const uint8_t *valLut, int w, int h,
                                   int cppD, int roD, bool isDirectDiff)
{
    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();
    const auto &lut = heatLUT();
    if (isDirectDiff)
    {
        if (grayDiff.stride() == static_cast<ptrdiff_t>(w) &&
            out.stride() == static_cast<ptrdiff_t>(w) * 3)
        {
            const size_t total = static_cast<size_t>(w) * h;
            const uint8_t *src = grayDiff.buffer->data();
            auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data());
            for (size_t i = 0; i < total; ++i)
                dst[i] = lut[valLut[src[i]]];
        }
        else
        {
            for (int y = 0; y < h; ++y)
            {
                const uint8_t *src =
                    grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
                auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data() +
                                                        static_cast<size_t>(y) * out.stride());
                for (int x = 0; x < w; ++x)
                    dst[x] = lut[valLut[src[x]]];
            }
        }
        return out;
    }
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
        auto *dst =
            reinterpret_cast<HeatRGB *>(out.buffer->data() + static_cast<size_t>(y) * out.stride());
        for (int x = 0; x < w; ++x)
            dst[x] = lut[valLut[src[x * cppD + roD]]];
    }
    return out;
}

static ImageData renderHighlightOverlay(const ImageData &grayDiff, const ImageData &base,
                                        const uint8_t *valLut, uint8_t threshold, int w, int h,
                                        int cppD, int roD, bool isDirectDiff)
{
    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();
    const bool hasBase =
        !base.isNull() && base.width >= w && base.height >= h &&
        (base.format == PixelFormat::RGB24 || base.format == PixelFormat::BGR24 ||
         base.format == PixelFormat::RGBA32 || base.format == PixelFormat::BGRA32 ||
         base.format == PixelFormat::Grayscale8);
    const int cppB = hasBase ? base.channelsPerPixel() : 0;
    const int minDiff = std::max<int>(threshold, 1);
    const auto &hlColorLUT = highlightColorLUT();

    if (!hasBase)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t rawV = isDirectDiff ? src[x] : src[x * cppD + roD];
                const uint8_t v = valLut[rawV];
                if (v >= minDiff)
                {
                    const auto &c = hlColorLUT[v];
                    dst[x * 3 + 0] = c[0];
                    dst[x * 3 + 1] = c[1];
                    dst[x * 3 + 2] = c[2];
                }
                else
                {
                    dst[x * 3 + 0] = 128;
                    dst[x * 3 + 1] = 128;
                    dst[x * 3 + 2] = 128;
                }
            }
        }
        return out;
    }

    if (base.format == PixelFormat::Grayscale8)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            const uint8_t *bs = base.buffer->data() + static_cast<size_t>(y) * base.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t rawV = isDirectDiff ? src[x] : src[x * cppD + roD];
                const uint8_t v = valLut[rawV];
                if (v >= minDiff)
                {
                    const auto &c = hlColorLUT[v];
                    dst[x * 3 + 0] = c[0];
                    dst[x * 3 + 1] = c[1];
                    dst[x * 3 + 2] = c[2];
                }
                else
                {
                    const uint8_t g = bs[x];
                    dst[x * 3 + 0] = g;
                    dst[x * 3 + 1] = g;
                    dst[x * 3 + 2] = g;
                }
            }
        }
        return out;
    }

    const bool isBGR = (base.format == PixelFormat::BGR24 || base.format == PixelFormat::BGRA32);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
        const uint8_t *bs = base.buffer->data() + static_cast<size_t>(y) * base.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        const uint8_t *bp = bs;
        for (int x = 0; x < w; ++x, dst += 3, bp += cppB)
        {
            const uint8_t rawV = isDirectDiff ? src[x] : src[x * cppD + roD];
            const uint8_t v = valLut[rawV];
            if (v >= minDiff)
            {
                const auto &c = hlColorLUT[v];
                dst[0] = c[0];
                dst[1] = c[1];
                dst[2] = c[2];
            }
            else
            {
                const int r = isBGR ? bp[2] : bp[0];
                const int g = bp[1];
                const int b = isBGR ? bp[0] : bp[2];
                const uint8_t gray = static_cast<uint8_t>((r * 30 + g * 59 + b * 11) / 100);
                dst[0] = gray;
                dst[1] = gray;
                dst[2] = gray;
            }
        }
    }
    return out;
}

ImageData DifferenceEngine::visualizeOverlay(const ImageData &grayDiff, const ImageData &base,
                                             uint8_t threshold, double gain, bool highlight)
{
    if (grayDiff.isNull())
        return ImageData();

    alignas(16) uint8_t valLut[256];
    for (int i = 0; i < 256; ++i)
    {
        int v = i;
        if (gain > 1.0)
            v = std::min(255, static_cast<int>(std::round(i * gain)));
        valLut[i] = (v >= threshold) ? static_cast<uint8_t>(v) : 0;
    }

    const int w = grayDiff.width;
    const int h = grayDiff.height;
    const int cppD = grayDiff.channelsPerPixel();
    const int roD = channelOffset(grayDiff.format, 0);
    const bool isDirectDiff = (cppD == 1 && roD == 0);

    if (!highlight)
        return renderHeatOverlay(grayDiff, valLut, w, h, cppD, roD, isDirectDiff);
    return renderHighlightOverlay(grayDiff, base, valLut, threshold, w, h, cppD, roD, isDirectDiff);
}
