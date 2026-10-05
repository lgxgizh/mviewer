#include "core/image/ImageStats.h"

#include <algorithm>
#include <cmath>

namespace mviewer::core
{

namespace
{

template <PixelFormat Fmt> struct PixelReader;

template <> struct PixelReader<PixelFormat::Grayscale8>
{
    static constexpr int cpp = 1;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        r = g = b = p[0];
    }
};

template <> struct PixelReader<PixelFormat::RGB24>
{
    static constexpr int cpp = 3;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        r = p[0];
        g = p[1];
        b = p[2];
    }
};

template <> struct PixelReader<PixelFormat::RGBA32>
{
    static constexpr int cpp = 4;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        r = p[0];
        g = p[1];
        b = p[2];
    }
};

template <> struct PixelReader<PixelFormat::BGR24>
{
    static constexpr int cpp = 3;
    static inline void read(const uint8_t *p, uint8_t &r, uint8_t &g, uint8_t &b)
    {
        b = p[0];
        g = p[1];
        r = p[2];
    }
};

template <> struct PixelReader<PixelFormat::BGRA32>
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
void accumulatePreview(const ImageBuffer &view, int x0, int x1, int y0, int y1, int64_t &sumR,
                       int64_t &sumG, int64_t &sumB, int64_t &sumL, int64_t &sumV, int64_t &count)
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
                if constexpr (Fmt == PixelFormat::RGBA32 || Fmt == PixelFormat::BGRA32)
                {
                    if (p[3] == 0)
                        continue;
                }
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

// Same 8-bit hue/saturation as PixelInspector::toColorSpace(..., HSV):
// H is degrees in 0..360 (0 when achromatic), S is 0..100. V is not returned;
// callers keep AnalysisEngine's max(R,G,B) in 0..255.
void pixelHueSat(uint8_t r8, uint8_t g8, uint8_t b8, double &hue, double &sat)
{
    const int r = static_cast<int>(r8);
    const int g = static_cast<int>(g8);
    const int b = static_cast<int>(b8);
    const int mx = std::max({r, g, b});
    const int mn = std::min({r, g, b});
    const int d = mx - mn;
    hue = 0.0;
    if (d > 0)
    {
        if (mx == r)
            hue = std::fmod(60.0 * (static_cast<double>(g - b) / d), 360.0);
        else if (mx == g)
            hue = 60.0 * (static_cast<double>(b - r) / d + 2.0);
        else
            hue = 60.0 * (static_cast<double>(r - g) / d + 4.0);
        if (hue < 0.0)
            hue += 360.0;
    }
    sat = mx > 0 ? (static_cast<double>(d) / static_cast<double>(mx)) * 100.0 : 0.0;
}

struct RoiAccum
{
    uint64_t sumR = 0;
    uint64_t sumG = 0;
    uint64_t sumB = 0;
    uint64_t sumV = 0;
    double sumS = 0.0;
    double sumSin = 0.0;
    double sumCos = 0.0;
    int64_t hueCount = 0;
    int64_t pixelCount = 0;
    bool cancelled = false;
};

constexpr double kDegToRad = 0.017453292519943295; // pi / 180
constexpr double kRadToDeg = 57.29577951308232;    // 180 / pi

double circularHueMean(double sumSin, double sumCos, int64_t hueCount)
{
    if (hueCount <= 0)
        return 0.0;
    double hue = std::atan2(sumSin, sumCos) * kRadToDeg;
    if (hue < 0.0)
        hue += 360.0;
    if (hue >= 360.0)
        hue = 0.0;
    return hue;
}

template <PixelFormat Fmt>
void accumulateROI(const ImageBuffer &view, int x0, int x1, int y0, int y1,
                   const std::function<bool()> &isCancelled, RoiAccum &accum)
{
    const ptrdiff_t stride = view.stride();
    if constexpr (Fmt == PixelFormat::Grayscale8)
    {
        for (int y = y0; y < y1; ++y)
        {
            if (isCancelled && isCancelled())
            {
                accum.cancelled = true;
                return;
            }
            const uint8_t *row = view.data + static_cast<size_t>(y) * stride;
            uint64_t rowSum = 0;
            for (int x = x0; x < x1; ++x)
                rowSum += row[x];
            accum.sumG += rowSum;
            accum.pixelCount += (x1 - x0);
        }
        // Replicated gray is achromatic: H = 0, S = 0, V = the gray value.
        accum.sumR = accum.sumB = accum.sumV = accum.sumG;
    }
    else
    {
        constexpr int cpp = PixelReader<Fmt>::cpp;
        for (int y = y0; y < y1; ++y)
        {
            if (isCancelled && isCancelled())
            {
                accum.cancelled = true;
                return;
            }
            const uint8_t *p =
                view.data + static_cast<size_t>(y) * stride + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                if constexpr (Fmt == PixelFormat::RGBA32 || Fmt == PixelFormat::BGRA32)
                {
                    if (p[3] == 0)
                        continue;
                }
                uint8_t r, g, b;
                PixelReader<Fmt>::read(p, r, g, b);
                accum.sumR += r;
                accum.sumG += g;
                accum.sumB += b;
                accum.sumV += std::max({r, g, b});
                double hue = 0.0;
                double sat = 0.0;
                pixelHueSat(r, g, b, hue, sat);
                accum.sumS += sat;
                if (sat > 0.0)
                {
                    const double rad = hue * kDegToRad;
                    accum.sumSin += std::sin(rad);
                    accum.sumCos += std::cos(rad);
                    ++accum.hueCount;
                }
                ++accum.pixelCount;
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

    const long long x0ll = std::clamp<long long>(region.x, 0, img.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, img.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, img.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, img.height);
    const int x0 = static_cast<int>((std::min)(x0ll, x1ll));
    const int y0 = static_cast<int>((std::min)(y0ll, y1ll));
    const int x1 = static_cast<int>((std::max)(x0ll, x1ll));
    const int y1 = static_cast<int>((std::max)(y0ll, y1ll));
    if (x1 <= x0 || y1 <= y0)
        return out;

    const ImageBuffer view = img.view();
    int64_t sumR = 0, sumG = 0, sumB = 0, sumL = 0, sumV = 0;
    int64_t count = 0;

    switch (view.format)
    {
    case PixelFormat::BGR24:
        accumulatePreview<PixelFormat::BGR24>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV,
                                              count);
        break;
    case PixelFormat::BGRA32:
        accumulatePreview<PixelFormat::BGRA32>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV,
                                               count);
        break;
    case PixelFormat::Grayscale8:
        accumulatePreview<PixelFormat::Grayscale8>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL,
                                                   sumV, count);
        break;
    case PixelFormat::RGBA32:
        accumulatePreview<PixelFormat::RGBA32>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV,
                                               count);
        break;
    case PixelFormat::RGB24:
    default:
        accumulatePreview<PixelFormat::RGB24>(view, x0, x1, y0, y1, sumR, sumG, sumB, sumL, sumV,
                                              count);
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

    RoiAccum accum;
    const ImageBuffer view = img.view();

    switch (view.format)
    {
    case PixelFormat::BGR24:
        accumulateROI<PixelFormat::BGR24>(view, x0, x1, y0, y1, isCancelled, accum);
        break;
    case PixelFormat::BGRA32:
        accumulateROI<PixelFormat::BGRA32>(view, x0, x1, y0, y1, isCancelled, accum);
        break;
    case PixelFormat::Grayscale8:
        accumulateROI<PixelFormat::Grayscale8>(view, x0, x1, y0, y1, isCancelled, accum);
        break;
    case PixelFormat::RGBA32:
        accumulateROI<PixelFormat::RGBA32>(view, x0, x1, y0, y1, isCancelled, accum);
        break;
    case PixelFormat::RGB24:
    default:
        accumulateROI<PixelFormat::RGB24>(view, x0, x1, y0, y1, isCancelled, accum);
        break;
    }

    if (accum.cancelled)
    {
        out = {};
        out.cancelled = true;
        return out;
    }
    if (accum.pixelCount <= 0)
        return out;

    out.pixelCount = accum.pixelCount;
    const double count = static_cast<double>(accum.pixelCount);
    out.rMean = static_cast<double>(accum.sumR) / count;
    out.gMean = static_cast<double>(accum.sumG) / count;
    out.bMean = static_cast<double>(accum.sumB) / count;
    out.vMean = static_cast<double>(accum.sumV) / count;
    out.sMean = accum.sumS / count;
    out.hMean = circularHueMean(accum.sumSin, accum.sumCos, accum.hueCount);
    out.valid = true;
    if (accum.sumG != 0)
    {
        out.rOverG = static_cast<double>(accum.sumR) / static_cast<double>(accum.sumG);
        out.bOverG = static_cast<double>(accum.sumB) / static_cast<double>(accum.sumG);
        out.ratiosValid = true;
    }
    return out;
}

} // namespace mviewer::core
