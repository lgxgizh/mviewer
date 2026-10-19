#include "core/analysis/AnalysisEngine.h"

#include "core/analyzer/PixelAccess.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/QtConvert.h"
#include "domain/Selection.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <sstream>

// Internal implementation: convert ImageData to QImage for pixel-level statistics; the algorithm logic stays the same.
// The header does not expose Qt; Qt is used inside the .cpp as an implementation detail.

ImageStats AnalysisEngine::computeStats(const ImageData &imgData)
{
    // Full-image statistics: set the ROI to the entire image
    mviewer::domain::Selection full;
    full.x = 0;
    full.y = 0;
    full.width = imgData.width;
    full.height = imgData.height;
    return computeStatsROI(imgData, full);
}

namespace
{

void finalizeStatsHistogramMetrics(ImageStats &s)
{
    if (s.pixelCount <= 0)
        return;

    auto calcChannel = [](const int64_t hist[256], int64_t count, double mean, double &stdDev,
                          int &minV, int &maxV)
    {
        double varSum = 0.0;
        minV = 255;
        maxV = 0;
        bool found = false;
        for (int i = 0; i < 256; ++i)
        {
            const int64_t cnt = hist[i];
            if (cnt > 0)
            {
                if (!found)
                {
                    minV = i;
                    found = true;
                }
                maxV = i;
                const double d = static_cast<double>(i) - mean;
                varSum += d * d * static_cast<double>(cnt);
            }
        }
        if (!found)
        {
            minV = 0;
            maxV = 0;
        }
        const double denom = static_cast<double>(count);
        stdDev = denom > 0.0 ? std::sqrt(std::max(0.0, varSum / denom)) : 0.0;
    };

    calcChannel(s.histLum, s.pixelCount, s.lumMean, s.lumStdDev, s.lumMin, s.lumMax);
    calcChannel(s.histR, s.pixelCount, s.rMean, s.rStdDev, s.rMin, s.rMax);
    calcChannel(s.histG, s.pixelCount, s.gMean, s.gStdDev, s.gMin, s.gMax);
    calcChannel(s.histB, s.pixelCount, s.bMean, s.bStdDev, s.bMin, s.bMax);
}

ImageStats computeStatsGrayscale(const ConstImageView &vbuf, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    if (rx == 0 && rw == vbuf.width && vbuf.stride() == static_cast<ptrdiff_t>(rw))
    {
        const uint8_t *p = vbuf.data + static_cast<size_t>(ry) * rw;
        const size_t total = static_cast<size_t>(rw) * rh;
        for (size_t i = 0; i < total; ++i)
        {
            ++s.histLum[p[i]];
        }
    }
    else
    {
        for (int y = ry; y < ry + rh; ++y)
        {
            const uint8_t *line = vbuf.data + static_cast<size_t>(y) * vbuf.stride();
            for (int x = rx; x < rx + rw; ++x)
            {
                ++s.histLum[line[x]];
            }
        }
    }
    long long sum = 0;
    for (int i = 0; i < 256; ++i)
    {
        const int64_t cnt = s.histLum[i];
        sum += static_cast<int64_t>(i) * cnt;
        s.histV[i] = cnt;
        s.histR[i] = cnt;
        s.histG[i] = cnt;
        s.histB[i] = cnt;
    }
    const int64_t count = static_cast<int64_t>(rw) * static_cast<int64_t>(rh);
    s.pixelCount = count;
    if (count > 0)
    {
        const double mean = static_cast<double>(sum) / static_cast<double>(count);
        s.lumMean = mean;
        s.vMean = mean;
        s.rMean = mean;
        s.gMean = mean;
        s.bMean = mean;
        finalizeStatsHistogramMetrics(s);
    }
    return s;
}

ImageStats computeStatsFallback(const ImageData &imgData, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    const QImage image = mvcore::toQImage(imgData).convertToFormat(QImage::Format_RGB32);
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
    int64_t count = 0;
    for (int y = ry; y < ry + rh; ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = rx; x < rx + rw; ++x)
        {
            const QRgb c = line[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
            sumR += r;
            sumG += g;
            sumB += b;
            const int lum = lumaRec601(Rgb8{r, g, b});
            sumL += lum;
            const int v = std::max({r, g, b});
            sumV += v;
            ++s.histLum[lum];
            ++s.histV[std::clamp(v, 0, 255)];
            ++s.histR[std::clamp(r, 0, 255)];
            ++s.histG[std::clamp(g, 0, 255)];
            ++s.histB[std::clamp(b, 0, 255)];
            ++count;
        }
    }
    s.pixelCount = count;
    if (count > 0)
    {
        const double samples = static_cast<double>(count);
        s.lumMean = static_cast<double>(sumL) / samples;
        s.vMean = static_cast<double>(sumV) / samples;
        s.rMean = static_cast<double>(sumR) / samples;
        s.gMean = static_cast<double>(sumG) / samples;
        s.bMean = static_cast<double>(sumB) / samples;
        finalizeStatsHistogramMetrics(s);
    }
    return s;
}

} // namespace

ImageStats AnalysisEngine::computeStatsROI(const ImageData &imgData,
                                           const mviewer::domain::Selection &region)
{
    if (imgData.isNull())
        return {};
    const int w = imgData.width;
    const int h = imgData.height;
    if (w <= 0 || h <= 0)
        return {};

    // Clip the ROI to the image bounds (64-bit to prevent overflow)
    const long long x0ll = std::clamp<long long>(region.x, 0, w);
    const long long y0ll = std::clamp<long long>(region.y, 0, h);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, w);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, h);
    const int rx = static_cast<int>(std::min(x0ll, x1ll));
    const int ry = static_cast<int>(std::min(y0ll, y1ll));
    const int rw = static_cast<int>(std::max(x0ll, x1ll)) - rx;
    const int rh = static_cast<int>(std::max(y0ll, y1ll)) - ry;
    if (rw <= 0 || rh <= 0)
        return {};

    const ConstImageView vbuf = imgData.constView();
    const PixelFormat fmt = imgData.format;
    if (fmt == PixelFormat::Grayscale8)
        return computeStatsGrayscale(vbuf, rx, ry, rw, rh);

    const bool isRgb24 = (fmt == PixelFormat::RGB24);
    const bool isBgr24 = (fmt == PixelFormat::BGR24);
    const bool isRgba32 = (fmt == PixelFormat::RGBA32);
    const bool isBgra32 = (fmt == PixelFormat::BGRA32);

    if (isRgb24 || isBgr24 || isRgba32 || isBgra32)
    {
        ImageStats s;
        const int cpp = vbuf.channelsPerPixel();
        const bool skipClear = isRgba32 || isBgra32;
        int64_t counted = 0;
        auto accumulatePixel = [&](const uint8_t *p)
        {
            if (skipClear && p[3] == 0)
                return;
            const Rgb8 rgb = unpackRgb8(p, fmt);
            const int lum = lumaRec601(rgb);
            const int v = (std::max)(rgb.r, (std::max)(rgb.g, rgb.b));
            ++s.histLum[lum];
            ++s.histV[v];
            ++s.histR[rgb.r];
            ++s.histG[rgb.g];
            ++s.histB[rgb.b];
            ++counted;
        };
        if (rx == 0 && rw == vbuf.width && vbuf.stride() == static_cast<ptrdiff_t>(rw) * cpp)
        {
            const uint8_t *p = vbuf.data + static_cast<size_t>(ry) * vbuf.stride();
            const size_t total = static_cast<size_t>(rw) * static_cast<size_t>(rh);
            for (size_t i = 0; i < total; ++i, p += cpp)
                accumulatePixel(p);
        }
        else
        {
            for (int y = ry; y < ry + rh; ++y)
            {
                const uint8_t *p = vbuf.data + static_cast<size_t>(y) * vbuf.stride() +
                                   static_cast<size_t>(rx) * cpp;
                for (int x = 0; x < rw; ++x, p += cpp)
                    accumulatePixel(p);
            }
        }
        const int64_t count = counted;
        s.pixelCount = count;
        if (count > 0)
        {
            int64_t sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
            for (int i = 0; i < 256; ++i)
            {
                const int64_t bin = i;
                sumL += bin * s.histLum[i];
                sumV += bin * s.histV[i];
                sumR += bin * s.histR[i];
                sumG += bin * s.histG[i];
                sumB += bin * s.histB[i];
            }
            const double samples = static_cast<double>(count);
            s.lumMean = static_cast<double>(sumL) / samples;
            s.vMean = static_cast<double>(sumV) / samples;
            s.rMean = static_cast<double>(sumR) / samples;
            s.gMean = static_cast<double>(sumG) / samples;
            s.bMean = static_cast<double>(sumB) / samples;
            finalizeStatsHistogramMetrics(s);
        }
        return s;
    }

    return computeStatsFallback(imgData, rx, ry, rw, rh);
}

ImageData AnalysisEngine::differenceMap(const ImageData &aData, const ImageData &bData)
{
    return DifferenceEngine::differenceMap(aData, bData, 0);
}

ImageData AnalysisEngine::heatMap(const ImageData &grayData)
{
    if (grayData.isNull())
        return ImageData();
    return DifferenceEngine::heatMap(grayData);
}

std::string ImageStats::toSummaryString() const
{
    std::ostringstream os;
    os << "Pixels: " << pixelCount << ", Lum: " << lumMean << " [std=" << lumStdDev
       << ", min=" << lumMin << ", max=" << lumMax << "]"
       << ", R: " << rMean << " [std=" << rStdDev << ", min=" << rMin << ", max=" << rMax << "]"
       << ", G: " << gMean << " [std=" << gStdDev << ", min=" << gMin << ", max=" << gMax << "]"
       << ", B: " << bMean << " [std=" << bStdDev << ", min=" << bMin << ", max=" << bMax << "]"
       << ", HSV-V: " << vMean;
    return os.str();
}

std::string ImageStats::toCsvRow() const
{
    std::ostringstream os;
    os << pixelCount << "," << lumMean << "," << lumStdDev << "," << lumMin << "," << lumMax << ","
       << rMean << "," << rStdDev << "," << rMin << "," << rMax << "," << gMean << "," << gStdDev
       << "," << gMin << "," << gMax << "," << bMean << "," << bStdDev << "," << bMin << "," << bMax
       << "," << vMean;
    return os.str();
}

std::string ImageStats::csvHeader()
{
    return "pixelCount,lumMean,lumStdDev,lumMin,lumMax,rMean,rStdDev,rMin,rMax,"
           "gMean,gStdDev,gMin,gMax,bMean,bStdDev,bMin,bMax,vMean";
}
