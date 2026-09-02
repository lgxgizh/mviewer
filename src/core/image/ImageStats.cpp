#include "core/image/ImageStats.h"

#include <algorithm>

namespace mviewer::core
{

PreviewStats computePreviewStats(const ImageData &img)
{
    return computePreviewStatsROI(
        img, mviewer::domain::Selection{0, 0, img.width, img.height});
}

PreviewStats computePreviewStatsROI(const ImageData &img,
                                    const mviewer::domain::Selection &region)
{
    PreviewStats out;
    if (img.isNull() || region.isEmpty())
        return out;

    const int x0 = std::max(0, region.x);
    const int y0 = std::max(0, region.y);
    const int x1 = std::min(img.width, region.x + region.width);
    const int y1 = std::min(img.height, region.y + region.height);
    if (x1 <= x0 || y1 <= y0)
        return out;

    const ImageBuffer view = img.view();
    const int cpp = view.channelsPerPixel();
    const ptrdiff_t stride = view.stride();
    int64_t sumR = 0, sumG = 0, sumB = 0, sumL = 0;
    int64_t count = 0;
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *row = view.data + static_cast<size_t>(y) * stride;
        for (int x = x0; x < x1; ++x)
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
            ++count;
        }
    }

    out.lumMean = static_cast<double>(sumL) / count;
    out.rMean = static_cast<int>(sumR / count);
    out.gMean = static_cast<int>(sumG / count);
    out.bMean = static_cast<int>(sumB / count);
    out.valid = true;
    return out;
}

ROIChannelStats computeROIChannelStats(const ImageData &img,
                                       const mviewer::domain::Selection &region)
{
    ROIChannelStats out;
    if (img.isNull() || region.isEmpty())
        return out;

    const long long x0ll = std::clamp<long long>(region.x, 0, img.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, img.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, img.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, img.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x1 <= x0 || y1 <= y0)
        return out;

    // long double keeps accumulation error bounded for very large source
    // regions while preserving the exact 8-bit channel values.
    long double sumR = 0.0L;
    long double sumG = 0.0L;
    long double sumB = 0.0L;
    const ImageBuffer view = img.view();
    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *row = view.data + static_cast<size_t>(y) * view.stride();
        for (int x = x0; x < x1; ++x)
        {
            const uint8_t *p = row + static_cast<size_t>(x) * view.channelsPerPixel();
            uint8_t r = 0, g = 0, b = 0;
            switch (view.format)
            {
            case PixelFormat::BGR24:
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
            ++out.pixelCount;
        }
    }
    if (out.pixelCount <= 0)
        return out;

    const long double count = static_cast<long double>(out.pixelCount);
    out.rMean = static_cast<double>(sumR / count);
    out.gMean = static_cast<double>(sumG / count);
    out.bMean = static_cast<double>(sumB / count);
    out.valid = true;
    if (sumG != 0.0L)
    {
        out.rOverG = static_cast<double>(sumR / sumG);
        out.bOverG = static_cast<double>(sumB / sumG);
        out.ratiosValid = true;
    }
    return out;
}

} // namespace mviewer::core
