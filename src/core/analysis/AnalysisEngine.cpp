#include "core/analysis/AnalysisEngine.h"

#include "core/compare/DifferenceEngine.h"
#include "core/image/QtConvert.h"
#include "domain/Selection.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

// 内部实现：把 ImageData 转成 QImage 做像素级统计，算法逻辑保持不变。
// header 不暴露 Qt；这里在 .cpp 内部使用 Qt 作为实现细节。

ImageStats AnalysisEngine::computeStats(const ImageData &imgData)
{
    // 全图统计：ROI 设为整图
    mviewer::domain::Selection full;
    full.x = 0;
    full.y = 0;
    full.width = imgData.width;
    full.height = imgData.height;
    return computeStatsROI(imgData, full);
}

namespace
{

ImageStats computeStatsGrayscale(const ImageBuffer &vbuf, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    long long sum = 0;
    if (rx == 0 && rw == vbuf.width && vbuf.stride() == static_cast<ptrdiff_t>(rw))
    {
        const uint8_t *p = vbuf.data + static_cast<size_t>(ry) * rw;
        const size_t total = static_cast<size_t>(rw) * rh;
        for (size_t i = 0; i < total; ++i)
        {
            const uint8_t val = p[i];
            sum += val;
            ++s.histLum[val];
        }
    }
    else
    {
        for (int y = ry; y < ry + rh; ++y)
        {
            const uint8_t *line = vbuf.data + static_cast<size_t>(y) * vbuf.stride();
            for (int x = rx; x < rx + rw; ++x)
            {
                const uint8_t val = line[x];
                sum += val;
                ++s.histLum[val];
            }
        }
    }
    for (int i = 0; i < 256; ++i)
    {
        s.histV[i] = s.histLum[i];
        s.histR[i] = s.histLum[i];
        s.histG[i] = s.histLum[i];
        s.histB[i] = s.histLum[i];
    }
    const int count = rw * rh;
    s.pixelCount = count;
    if (count > 0)
    {
        const double mean = static_cast<double>(sum) / count;
        s.lumMean = mean;
        s.vMean = mean;
        s.rMean = mean;
        s.gMean = mean;
        s.bMean = mean;
    }
    return s;
}

ImageStats computeStatsFallback(const ImageData &imgData, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    const QImage image = mvcore::toQImage(imgData).convertToFormat(QImage::Format_RGB32);
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
    int count = 0;
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
            const int lum = (19595 * r + 38470 * g + 7471 * b) >> 16;
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
        s.lumMean = static_cast<double>(sumL) / count;
        s.vMean = static_cast<double>(sumV) / count;
        s.rMean = static_cast<double>(sumR) / count;
        s.gMean = static_cast<double>(sumG) / count;
        s.bMean = static_cast<double>(sumB) / count;
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

    // 裁剪 ROI 到图像边界 (64位防溢出)
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

    const ImageBuffer vbuf = imgData.view();
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
        const bool isBGR = (isBgr24 || isBgra32);
        const int rIdx = isBGR ? 2 : 0;
        const int bIdx = isBGR ? 0 : 2;
        long long sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
        int count = 0;
        for (int y = ry; y < ry + rh; ++y)
        {
            const uint8_t *line = vbuf.data + static_cast<size_t>(y) * vbuf.stride();
            for (int x = rx; x < rx + rw; ++x)
            {
                const uint8_t *p = line + static_cast<size_t>(x) * cpp;
                const int r = p[rIdx];
                const int g = p[1];
                const int b = p[bIdx];
                sumR += r;
                sumG += g;
                sumB += b;
                const int lum = (19595 * r + 38470 * g + 7471 * b) >> 16;
                sumL += lum;
                const int v = std::max({r, g, b});
                sumV += v;
                ++s.histLum[lum];
                ++s.histV[v];
                ++s.histR[r];
                ++s.histG[g];
                ++s.histB[b];
                ++count;
            }
        }
        s.pixelCount = count;
        if (count > 0)
        {
            s.lumMean = static_cast<double>(sumL) / count;
            s.vMean = static_cast<double>(sumV) / count;
            s.rMean = static_cast<double>(sumR) / count;
            s.gMean = static_cast<double>(sumG) / count;
            s.bMean = static_cast<double>(sumB) / count;
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
