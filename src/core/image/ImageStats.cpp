#include "core/image/ImageStats.h"

#include <algorithm>

namespace mviewer::core
{

namespace
{

template <PixelFormat Fmt>
struct PixelReader;

template <>
struct PixelReader<PixelFormat::Grayscale8>
{
    static constexpr int cpp = 1;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        r = g = b = p[0];
    }
};

template <>
struct PixelReader<PixelFormat::RGB24>
{
    static constexpr int cpp = 3;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        r = p[0];
        g = p[1];
        b = p[2];
    }
};

template <>
struct PixelReader<PixelFormat::RGBA32>
{
    static constexpr int cpp = 4;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        r = p[0];
        g = p[1];
        b = p[2];
    }
};

template <>
struct PixelReader<PixelFormat::BGR24>
{
    static constexpr int cpp = 3;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        b = p[0];
        g = p[1];
        r = p[2];
    }
};

template <>
struct PixelReader<PixelFormat::BGRA32>
{
    static constexpr int cpp = 4;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        b = p[0];
        g = p[1];
        r = p[2];
    }
};

template <PixelFormat Fmt>
void accumulatePreview(const ImageBuffer &view, int x0, int x1, int y0, int y1,
                       int64_t &sumR, int64_t &sumG, int64_t &sumB, int64_t &sumL, int64_t &sumV,
                       int64_t &count)
{
    const ptrdiff_t stride = view.stride();
    if constexpr (Fmt == PixelFormat::Grayscale8)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *row = view.data + static_cast<size_t>(y) * stride;
            int64_t rowSum = 0;
            for (int x = x0; x < x1; ++x)
            {
                rowSum += row[x];
            }
            sumG += rowSum;
            count += (x1 - x0);
        }
        sumR = sumB = sumL = sumV = sumG;
    }
    else
    {
        constexpr int cpp = PixelReader<Fmt>::cpp;
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *p =
                view.data + static_cast<size_t>(y) * stride + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                uint8_t r, g, b;
                PixelReader<Fmt>::read(p, r, g, b);
                sumR += r;
                sumG += g;
                sumB += b;
                sumL += luminance(r, g, b);
                sumV += std::max({r, g, b});
                ++count;
            }
        }
    }
}

template <PixelFormat Fmt>
void accumulateROI(const ImageBuffer &view, int x0, int x1, int y0, int y1,
                   const std::function<bool()> &isCancelled,
                   uint64_t &sumR, uint64_t &sumG, uint64_t &sumB, uint64_t &sumV,
                   int64_t &pixelCount, bool &cancelled)
{
    const ptrdiff_t stride = view.stride();
    if constexpr (Fmt == PixelFormat::Grayscale8)
    {
        for (int y = y0; y < y1; ++y)
        {
            if (isCancelled && isCancelled())
            {
                cancelled = true;
                return;
            }
            const uint8_t *row = view.data + static_cast<size_t>(y) * stride;
            uint64_t rowSum = 0;
            for (int x = x0; x < x1; ++x)
            {
                rowSum += row[x];
            }
            sumG += rowSum;
            pixelCount += (x1 - x0);
        }
        sumR = sumB = sumV = sumG;
    }
    else
    {
        constexpr int cpp = PixelReader<Fmt>::cpp;
        for (int y = y0; y < y1; ++y)
        {
            if (isCancelled && isCancelled())
            {
                cancelled = true;
                return;
            }
            const uint8_t *p =
                view.data + static_cast<size_t>(y) * stride + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                uint8_t r, g, b;
                PixelReader<Fmt>::read(p, r, g, b);
                sumR += r;
                sumG += g;
                sumB += b;
                sumV += std::max({r, g, b});
                ++pixelCount;
            }
        }
    }
}

} // namespace

PreviewStats computePreviewStats(const ImageData &img)
{
    return computePreviewStatsROI(img, mviewer::domain::Selection{0, 0, img.width, img.height});
}

PreviewStats computePreviewStatsROI(const ImageData &img, const mviewer::domain::Selection &region)
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
    int64_t sumR = 0, sumG = 0, sumB = 0, sumL = 0, sumV = 0;
    int64_t count = 0;

    switch (view.format)
    {
    case PixelFormat::BGR24:
        accumulatePreview<PixelFormat::BGR24>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV, count);
        break;
    case PixelFormat::BGRA32:
        accumulatePreview<PixelFormat::BGRA32>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV, count);
        break;
    case PixelFormat::Grayscale8:
        accumulatePreview<PixelFormat::Grayscale8>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV, count);
        break;
    case PixelFormat::RGBA32:
        accumulatePreview<PixelFormat::RGBA32>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV, count);
        break;
    case PixelFormat::RGB24:
    default:
        accumulatePreview<PixelFormat::RGB24>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV, count);
        break;
    }

    if (count <= 0)
        return out;

    out.lumMean = static_cast<double>(sumL) / static_cast<double>(count);
    out.vMean = static_cast<double>(sumV) / static_cast<double>(count);
    out.rMean = static_cast<int>(sumR / count);
    out.gMean = static_cast<int>(sumG / count);
    out.bMean = static_cast<int>(sumB / count);
    out.valid = true;
    return out;
}

ROIChannelStats computeROIChannelStats(const ImageData &img,
                                       const mviewer::domain::Selection &region)
{
    return computeROIChannelStats(img, region, {});
}

ROIChannelStats computeROIChannelStats(const ImageData &img,
                                       const mviewer::domain::Selection &region,
                                       const std::function<bool()> &isCancelled)
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

    uint64_t sumR = 0;
    uint64_t sumG = 0;
    uint64_t sumB = 0;
    uint64_t sumV = 0;
    bool cancelled = false;
    const ImageBuffer view = img.view();

    switch (view.format)
    {
    case PixelFormat::BGR24:
        accumulateROI<PixelFormat::BGR24>(view, x0, x1, y0, y1, isCancelled,
                                          sumR, sumG, sumB, sumV, out.pixelCount, cancelled);
        break;
    case PixelFormat::BGRA32:
        accumulateROI<PixelFormat::BGRA32>(view, x0, x1, y0, y1, isCancelled,
                                          sumR, sumG, sumB, sumV, out.pixelCount, cancelled);
        break;
    case PixelFormat::Grayscale8:
        accumulateROI<PixelFormat::Grayscale8>(view, x0, x1, y0, y1, isCancelled,
                                              sumR, sumG, sumB, sumV, out.pixelCount, cancelled);
        break;
    case PixelFormat::RGBA32:
        accumulateROI<PixelFormat::RGBA32>(view, x0, x1, y0, y1, isCancelled,
                                          sumR, sumG, sumB, sumV, out.pixelCount, cancelled);
        break;
    case PixelFormat::RGB24:
    default:
        accumulateROI<PixelFormat::RGB24>(view, x0, x1, y0, y1, isCancelled,
                                          sumR, sumG, sumB, sumV, out.pixelCount, cancelled);
        break;
    }

    if (cancelled)
    {
        out = {};
        out.cancelled = true;
        return out;
    }
    if (out.pixelCount <= 0)
        return out;

    const double count = static_cast<double>(out.pixelCount);
    out.rMean = static_cast<double>(sumR) / count;
    out.gMean = static_cast<double>(sumG) / count;
    out.bMean = static_cast<double>(sumB) / count;
    out.vMean = static_cast<double>(sumV) / count;
    out.valid = true;
    if (sumG != 0)
    {
        out.rOverG = static_cast<double>(sumR) / static_cast<double>(sumG);
        out.bOverG = static_cast<double>(sumB) / static_cast<double>(sumG);
        out.ratiosValid = true;
    }
    return out;
}

} // namespace mviewer::core
