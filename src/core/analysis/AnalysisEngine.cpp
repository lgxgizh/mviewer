#include "core/analysis/AnalysisEngine.h"

#include "core/compare/DifferenceEngine.h"
#include "core/image/QtConvert.h"
#include "domain/Selection.h"

#include <QImage>
#include <algorithm>
#include <cmath>
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
            const int lum = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
            sumL += lum;
            const int v = std::max({r, g, b});
            sumV += v;
            ++s.histLum[std::clamp(lum, 0, 255)];
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

    // 裁剪 ROI 到图像边界
    const int rx = std::max(0, region.x);
    const int ry = std::max(0, region.y);
    const int rw = std::min(region.width, w - rx);
    const int rh = std::min(region.height, h - ry);
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
        long long sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
        int count = 0;
        for (int y = ry; y < ry + rh; ++y)
        {
            const uint8_t *line = vbuf.data + static_cast<size_t>(y) * vbuf.stride();
            for (int x = rx; x < rx + rw; ++x)
            {
                const uint8_t *p = line + static_cast<size_t>(x) * cpp;
                const int r = isBGR ? p[2] : p[0];
                const int g = p[1];
                const int b = isBGR ? p[0] : p[2];
                sumR += r;
                sumG += g;
                sumB += b;
                const int lum = static_cast<int>(0.299 * r + 0.587 * g + 0.114 * b);
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
    // ONE implementation: DifferenceEngine handles every PixelFormat natively
    // (per-format channel offsets) and produces the same Grayscale8 map, so this
    // entry point only supplies the "no threshold" default. Keeping a second
    // copy here meant two sets of numerics for one user-visible feature.
    return DifferenceEngine::differenceMap(aData, bData, 0);
}

double AnalysisEngine::psnr(const ImageData &aData, const ImageData &bData)
{
    const int w = std::min(aData.width, bData.width);
    const int h = std::min(aData.height, bData.height);
    if (w == 0 || h == 0)
        return 0.0;

    int64_t sumSq = 0;
    const long long n = 1LL * w * h;
    const bool isSameFormat = (aData.format == bData.format);

    if (isSameFormat &&
        (aData.format == PixelFormat::RGB24 || aData.format == PixelFormat::BGR24 ||
         aData.format == PixelFormat::RGBA32 || aData.format == PixelFormat::BGRA32 ||
         aData.format == PixelFormat::Grayscale8))
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        const int cpp = va.channelsPerPixel();
        const bool isGray = (aData.format == PixelFormat::Grayscale8);

        if (isGray)
        {
            for (int y = 0; y < h; ++y)
            {
                const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
                const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
                for (int x = 0; x < w; ++x)
                {
                    const int d = static_cast<int>(la[x]) - static_cast<int>(lb[x]);
                    sumSq += d * d;
                }
            }
            const double mse = static_cast<double>(sumSq) / static_cast<double>(n);
            if (mse <= 1e-10)
                return 100.0; // 完美一致(而非 inf)
            return 10.0 * std::log10(65025.0 / mse);
        }

        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t *pa = la + static_cast<size_t>(x) * cpp;
                const uint8_t *pb = lb + static_cast<size_t>(x) * cpp;
                const int dr = static_cast<int>(pa[0]) - static_cast<int>(pb[0]);
                const int dg = static_cast<int>(pa[1]) - static_cast<int>(pb[1]);
                const int db = static_cast<int>(pa[2]) - static_cast<int>(pb[2]);
                sumSq += dr * dr + dg * dg + db * db;
            }
        }
        const double mse = static_cast<double>(sumSq) / static_cast<double>(n * 3);
        if (mse <= 1e-10)
            return 100.0; // 完美一致(而非 inf)
        return 10.0 * std::log10(65025.0 / mse);
    }

    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_RGB32);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        const QRgb *la = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int dr = static_cast<int>(qRed(la[x])) - qRed(lb[x]);
            const int dg = static_cast<int>(qGreen(la[x])) - qGreen(lb[x]);
            const int db = static_cast<int>(qBlue(la[x])) - qBlue(lb[x]);
            sumSq += dr * dr + dg * dg + db * db;
        }
    }
    const double mse = static_cast<double>(sumSq) / static_cast<double>(n * 3);
    if (mse <= 1e-10)
        return 100.0; // 完美一致(而非 inf)
    return 10.0 * std::log10(65025.0 / mse);
}

double AnalysisEngine::ssim(const ImageData &aData, const ImageData &bData)
{
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_Grayscale8);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_Grayscale8);
    const int w = std::min(aa.width(), bb.width());
    const int h = std::min(aa.height(), bb.height());
    if (w < 8 || h < 8)
        return 0.0;

    const double C1 = (0.01 * 255.0) * (0.01 * 255.0);
    const double C2 = (0.03 * 255.0) * (0.03 * 255.0);
    constexpr int block = 8;
    constexpr double N = 64.0;

    double ssimSum = 0.0;
    int blocks = 0;
    for (int by = 0; by + block <= h; by += block)
    {
        for (int bx = 0; bx + block <= w; bx += block)
        {
            int sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
            for (int y = 0; y < block; ++y)
            {
                const uchar *lineA = aa.constScanLine(by + y);
                const uchar *lineB = bb.constScanLine(by + y);
                for (int x = 0; x < block; ++x)
                {
                    const int pa = lineA[bx + x];
                    const int pb = lineB[bx + x];
                    sumA += pa;
                    sumB += pb;
                    sumAA += pa * pa;
                    sumBB += pb * pb;
                    sumAB += pa * pb;
                }
            }
            const double meanA = sumA / N;
            const double meanB = sumB / N;
            const double varA = std::max(0.0, (sumAA / N) - meanA * meanA);
            const double varB = std::max(0.0, (sumBB / N) - meanB * meanB);
            const double cov = (sumAB / N) - meanA * meanB;
            const double num = (2.0 * meanA * meanB + C1) * (2.0 * cov + C2);
            const double den = (meanA * meanA + meanB * meanB + C1) * (varA + varB + C2);
            ssimSum += num / den;
            ++blocks;
        }
    }
    return blocks > 0 ? ssimSum / blocks : 0.0;
}

double AnalysisEngine::noiseEstimate(const ImageData &imgData)
{
    if (imgData.isNull())
        return 0.0;
    QImage img = mvcore::toQImage(imgData).convertToFormat(QImage::Format_Grayscale8);
    const int w = img.width();
    const int h = img.height();
    if (w < 3 || h < 3)
        return 0.0;

    // 拉普拉斯算子 (3x3): [0 1 0; 1 -4 1; 0 1 0]
    // 噪声估计 = 拉普拉斯响应的方差 * (调整因子)
    // 参考: variance-of-Laplacian 方法
    int64_t sum = 0;
    int64_t sumSq = 0;
    int count = 0;
    for (int y = 1; y < h - 1; ++y)
    {
        const uchar *prev = img.constScanLine(y - 1);
        const uchar *curr = img.constScanLine(y);
        const uchar *next = img.constScanLine(y + 1);
        for (int x = 1; x < w - 1; ++x)
        {
            // 拉普拉斯响应
            const int lap = static_cast<int>(prev[x]) + curr[x - 1] + curr[x + 1] + next[x] -
                            4 * static_cast<int>(curr[x]);
            sum += lap;
            sumSq += static_cast<int64_t>(lap) * lap;
            ++count;
        }
    }
    if (count < 2)
        return 0.0;
    const double mean = static_cast<double>(sum) / count;
    const double variance = static_cast<double>(sumSq) / count - mean * mean;
    return std::max(0.0, variance);
}

ImageData AnalysisEngine::heatMap(const ImageData &grayData)
{
    if (grayData.isNull())
        return ImageData();
    QImage src = mvcore::toQImage(grayData);
    src = src.format() == QImage::Format_Grayscale8
              ? src
              : src.convertToFormat(QImage::Format_Grayscale8);
    const int w = src.width();
    const int h = src.height();
    QImage out(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        const uchar *line = src.constScanLine(y);
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int v = line[x]; // 0..255
            const int r = std::min(255, std::max(0, v * 2 - 255)) + std::min(255, v);
            const int g = std::min(255, std::max(0, 255 - std::abs(v - 128) * 2)) + v / 2;
            const int b = std::min(255, std::max(0, 255 - v * 2)) + std::min(255, 255 - v);
            dst[x] = qRgb(std::min(255, r / 2), std::min(255, g / 2), std::min(255, b / 2));
        }
    }
    return mvcore::fromQImage(out);
}
