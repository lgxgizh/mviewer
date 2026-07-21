#include "core/compare/DifferenceEngine.h"

#include <cstring>

int DifferenceEngine::channelOffset(PixelFormat fmt, int channel)
{
    switch (fmt)
    {
    case PixelFormat::RGB24:
        return channel;
    case PixelFormat::BGR24:
        return 2 - channel;
    case PixelFormat::RGBA32:
        return channel;
    case PixelFormat::BGRA32:
        return 2 - channel;
    case PixelFormat::Grayscale8:
        return 0;
    default:
        return channel;
    }
}

ImageData DifferenceEngine::differenceMap(const ImageData &a, const ImageData &b, uint8_t threshold)
{
    if (a.isNull() || b.isNull())
        return ImageData();
    if (a.format != PixelFormat::RGB24 && a.format != PixelFormat::BGR24 &&
        a.format != PixelFormat::RGBA32 && a.format != PixelFormat::BGRA32 &&
        a.format != PixelFormat::Grayscale8)
        return ImageData();
    if (b.format != PixelFormat::RGB24 && b.format != PixelFormat::BGR24 &&
        b.format != PixelFormat::RGBA32 && b.format != PixelFormat::BGRA32 &&
        b.format != PixelFormat::Grayscale8)
        return ImageData();

    const int w = std::min(a.width, b.width);
    const int h = std::min(a.height, b.height);
    const int cppA = a.channelsPerPixel();
    const int cppB = b.channelsPerPixel();
    const int roA0 = channelOffset(a.format, 0);
    const int roA1 = channelOffset(a.format, 1);
    const int roA2 = channelOffset(a.format, 2);
    const int roB0 = channelOffset(b.format, 0);
    const int roB1 = channelOffset(b.format, 1);
    const int roB2 = channelOffset(b.format, 2);

    ImageData out = makeImageData(w, h, PixelFormat::Grayscale8);
    if (out.isNull())
        return ImageData();

    for (int y = 0; y < h; ++y)
    {
        const uint8_t *la = a.buffer->data() + static_cast<size_t>(y) * a.stride();
        const uint8_t *lb = b.buffer->data() + static_cast<size_t>(y) * b.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < w; ++x)
        {
            const int dr = std::abs(static_cast<int>(la[x * cppA + roA0]) -
                                    static_cast<int>(lb[x * cppB + roB0]));
            const int dg = std::abs(static_cast<int>(la[x * cppA + roA1]) -
                                    static_cast<int>(lb[x * cppB + roB1]));
            const int db = std::abs(static_cast<int>(la[x * cppA + roA2]) -
                                    static_cast<int>(lb[x * cppB + roB2]));
            const uint8_t diff = static_cast<uint8_t>((dr + dg + db) / 3);
            // M15: apply threshold — only highlight pixels above threshold
            dst[x] = (diff > threshold) ? diff : 0;
        }
    }
    return out;
}

ImageData DifferenceEngine::applyThreshold(const ImageData &gray, uint8_t threshold)
{
    if (gray.isNull())
        return ImageData();
    ImageData out = makeImageData(gray.width, gray.height, gray.format);
    if (out.isNull())
        return ImageData();
    const int n = gray.width * gray.height;
    const int cpp = gray.channelsPerPixel();
    const int ro = channelOffset(gray.format, 0);
    for (int y = 0; y < gray.height; ++y)
    {
        const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < gray.width; ++x)
        {
            const uint8_t v = src[x * cpp + ro];
            dst[x] = (v > threshold) ? v : 0;
        }
    }
    return out;
}

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

    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < w; ++x)
        {
            const int v = src[x * cpp + ro];
            int r, g, b;
            if (v < 128)
            {
                r = 0;
                g = v * 2;
                b = 255 - v * 2;
            }
            else
            {
                r = (v - 128) * 2;
                g = 255 - (v - 128) * 2;
                b = 0;
            }
            dst[x * 3 + 0] = static_cast<uint8_t>(r);
            dst[x * 3 + 1] = static_cast<uint8_t>(g);
            dst[x * 3 + 2] = static_cast<uint8_t>(b);
        }
    }
    return out;
}
