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
    if (rx == 0 && rw == vbuf.width && vbuf.stride() == static_cast<size_t>(rw))
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

template <typename LineGetterA, typename LineGetterB>
double computeSSIMCore(int w, int h, LineGetterA &&lineA, LineGetterB &&lineB)
{
    const double C1 = (0.01 * 255.0) * (0.01 * 255.0);
    const double C2 = (0.03 * 255.0) * (0.03 * 255.0);
    constexpr int block = 8;
    constexpr double N = 64.0;

    double ssimSum = 0.0;
    int blocks = 0;
    for (int by = 0; by + block <= h; by += block)
    {
        const uint8_t *linesA[block];
        const uint8_t *linesB[block];
        for (int y = 0; y < block; ++y)
        {
            linesA[y] = lineA(by + y);
            linesB[y] = lineB(by + y);
        }

        for (int bx = 0; bx + block <= w; bx += block)
        {
            int sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
            for (int y = 0; y < block; ++y)
            {
                const uint8_t *la = linesA[y] + bx;
                const uint8_t *lb = linesB[y] + bx;
                for (int x = 0; x < block; ++x)
                {
                    const int pa = la[x];
                    const int pb = lb[x];
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

template <typename LineGetter>
double calcLaplacianCore(int w, int h, LineGetter &&getLine)
{
    int64_t sum = 0;
    int64_t sumSq = 0;
    int count = 0;
    for (int y = 1; y < h - 1; ++y)
    {
        const uint8_t *prev = getLine(y - 1);
        const uint8_t *curr = getLine(y);
        const uint8_t *next = getLine(y + 1);
        for (int x = 1; x < w - 1; ++x)
        {
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
    const int w = std::min(aData.width, bData.width);
    const int h = std::min(aData.height, bData.height);
    if (w < 8 || h < 8)
        return 0.0;

    if (aData.format == PixelFormat::Grayscale8 && bData.format == PixelFormat::Grayscale8)
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        return computeSSIMCore(
            w, h, [&va](int y) { return va.data + static_cast<size_t>(y) * va.stride(); },
            [&vb](int y) { return vb.data + static_cast<size_t>(y) * vb.stride(); });
    }

    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_Grayscale8);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_Grayscale8);
    return computeSSIMCore(
        w, h, [&aa](int y) { return aa.constScanLine(y); },
        [&bb](int y) { return bb.constScanLine(y); });
}

double AnalysisEngine::noiseEstimate(const ImageData &imgData)
{
    if (imgData.isNull())
        return 0.0;
    const int w = imgData.width;
    const int h = imgData.height;
    if (w < 3 || h < 3)
        return 0.0;

    if (imgData.format == PixelFormat::Grayscale8)
    {
        const ImageBuffer v = imgData.view();
        return calcLaplacianCore(w, h, [&v](int y) {
            return v.data + static_cast<size_t>(y) * v.stride();
        });
    }

    QImage img = mvcore::toQImage(imgData).convertToFormat(QImage::Format_Grayscale8);
    return calcLaplacianCore(w, h, [&img](int y) { return img.constScanLine(y); });
}

ImageData AnalysisEngine::heatMap(const ImageData &grayData)
{
    if (grayData.isNull())
        return ImageData();
    return DifferenceEngine::heatMap(grayData);
}
