#include "core/image/ImageStats.h"

namespace mviewer::core
{

PreviewStats computePreviewStats(const ImageData &img)
{
    PreviewStats out;
    if (img.isNull())
        return out;

    const ImageBuffer view = img.view();
    const int w = view.width;
    const int h = view.height;
    const int cpp = view.channelsPerPixel();
    const ptrdiff_t stride = view.stride();

    int64_t sumR = 0, sumG = 0, sumB = 0, sumL = 0;
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *row = view.data + static_cast<size_t>(y) * stride;
        for (int x = 0; x < w; ++x)
        {
            const uint8_t *p = row + static_cast<size_t>(x) * cpp;
            uint8_t r, g, b;
            switch (view.format)
            {
            case PixelFormat::BGR24:
                b = p[0];
                g = p[1];
                r = p[2];
                break;
            case PixelFormat::BGRA32:
                b = p[0];
                g = p[1];
                r = p[2];
                break;
            case PixelFormat::Grayscale8:
                r = g = b = p[0];
                break;
            case PixelFormat::RGB24:
            case PixelFormat::RGBA32:
            default:
                r = p[0];
                g = p[1];
                b = p[2];
                break;
            }
            sumR += r;
            sumG += g;
            sumB += b;
            sumL += luminance(r, g, b);
        }
    }

    const int64_t n = static_cast<int64_t>(w) * h;
    out.lumMean = static_cast<double>(sumL) / n;
    out.rMean = static_cast<int>(sumR / n);
    out.gMean = static_cast<int>(sumG / n);
    out.bMean = static_cast<int>(sumB / n);
    out.valid = true;
    return out;
}

} // namespace mviewer::core
