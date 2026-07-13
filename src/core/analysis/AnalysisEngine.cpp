#include "core/analysis/AnalysisEngine.h"
#include "core/image/QtConvert.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>

// 内部实现：把 ImageData 转成 QImage 做像素级统计，算法逻辑保持不变。
// header 不暴露 Qt；这里在 .cpp 内部使用 Qt 作为实现细节。

ImageStats AnalysisEngine::computeStats(const ImageData &imgData)
{
    ImageStats s;
    if (imgData.isNull()) return s;
    const QImage image = mvcore::toQImage(imgData).convertToFormat(QImage::Format_RGB32);
    const int w = image.width();
    const int h = image.height();
    const int n = w * h;
    if (n == 0) return s;

    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0;
    for (int y = 0; y < h; ++y) {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const QRgb c = line[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
            sumR += r; sumG += g; sumB += b;
            const int lum = static_cast<int>(0.299*r + 0.587*g + 0.114*b);
            sumL += lum;
            ++s.histLum[std::clamp(lum,0,255)];
            ++s.histR[std::clamp(r,0,255)];
            ++s.histG[std::clamp(g,0,255)];
            ++s.histB[std::clamp(b,0,255)];
        }
    }
    s.lumMean = static_cast<double>(sumL) / n;
    s.rMean = static_cast<double>(sumR) / n;
    s.gMean = static_cast<double>(sumG) / n;
    s.bMean = static_cast<double>(sumB) / n;
    return s;
}

ImageData AnalysisEngine::differenceMap(const ImageData &aData, const ImageData &bData)
{
    if (aData.isNull() || bData.isNull()) return ImageData();
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_RGB32);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_RGB32);
    const int w = std::min(aa.width(), bb.width());
    const int h = std::min(aa.height(), bb.height());
    QImage out(w, h, QImage::Format_Grayscale8);
    if (out.isNull()) return ImageData();
    for (int y = 0; y < h; ++y) {
        const QRgb *la = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        uchar *dst = out.scanLine(y);
        for (int x = 0; x < w; ++x) {
            const int dr = abs(static_cast<int>(qRed(la[x])) - qRed(lb[x]));
            const int dg = abs(static_cast<int>(qGreen(la[x])) - qGreen(lb[x]));
            const int db = abs(static_cast<int>(qBlue(la[x])) - qBlue(lb[x]));
            dst[x] = static_cast<uchar>(std::min(255, (dr+dg+db)/3));
        }
    }
    return mvcore::fromQImage(out);
}

double AnalysisEngine::psnr(const ImageData &aData, const ImageData &bData)
{
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_RGB32);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_RGB32);
    const int w = std::min(aa.width(), bb.width());
    const int h = std::min(aa.height(), bb.height());
    if (w == 0 || h == 0) return 0.0;

    double mse = 0.0;
    for (int y = 0; y < h; ++y) {
        const QRgb *la = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int dr = static_cast<int>(qRed(la[x])) - qRed(lb[x]);
            const int dg = static_cast<int>(qGreen(la[x])) - qGreen(lb[x]);
            const int db = static_cast<int>(qBlue(la[x])) - qBlue(lb[x]);
            mse += dr*dr + dg*dg + db*db;
        }
    }
    const int n = w * h;
    mse /= (n * 3);
    if (mse <= 1e-10) return 100.0; // 完美一致(而非 inf)
    return 10.0 * std::log10(65025.0 / mse);
}

double AnalysisEngine::ssim(const ImageData &aData, const ImageData &bData)
{
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_Grayscale8);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_Grayscale8);
    const int w = std::min(aa.width(), bb.width());
    const int h = std::min(aa.height(), bb.height());
    if (w < 8 || h < 8) return 0.0;

    const double C1 = (0.01*255)*(0.01*255);
    const double C2 = (0.03*255)*(0.03*255);
    const int block = 8;

    double ssimSum = 0.0;
    int blocks = 0;
    for (int by = 0; by + block <= h; by += block) {
        for (int bx = 0; bx + block <= w; bx += block) {
            double meanA = 0, meanB = 0, varA = 0, varB = 0, cov = 0;
            for (int y = 0; y < block; ++y) {
                for (int x = 0; x < block; ++x) {
                    double pa = qRed(aa.pixel(bx+x, by+y));
                    double pb = qRed(bb.pixel(bx+x, by+y));
                    meanA += pa; meanB += pb;
                }
            }
            const int N = block*block;
            meanA /= N; meanB /= N;
            for (int y = 0; y < block; ++y) {
                for (int x = 0; x < block; ++x) {
                    double pa = qRed(aa.pixel(bx+x, by+y)) - meanA;
                    double pb = qRed(bb.pixel(bx+x, by+y)) - meanB;
                    varA += pa*pa; varB += pb*pb; cov += pa*pb;
                }
            }
            varA /= N; varB /= N; cov /= N;
            const double num = (2*meanA*meanB + C1) * (2*cov + C2);
            const double den = (meanA*meanA + meanB*meanB + C1) * (varA + varB + C2);
            ssimSum += num / den;
            ++blocks;
        }
    }
    return blocks > 0 ? ssimSum / blocks : 0.0;
}

ImageData AnalysisEngine::heatMap(const ImageData &grayData)
{
    if (grayData.isNull()) return ImageData();
    QImage src = mvcore::toQImage(grayData);
    src = src.format() == QImage::Format_Grayscale8 ? src : src.convertToFormat(QImage::Format_Grayscale8);
    const int w = src.width();
    const int h = src.height();
    QImage out(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y) {
        const uchar *line = src.constScanLine(y);
        QRgb *dst = reinterpret_cast<QRgb *>(out.scanLine(y));
        for (int x = 0; x < w; ++x) {
            const int v = line[x]; // 0..255
            const int r = std::min(255, std::max(0, v*2 - 255)) + std::min(255, v);
            const int g = std::min(255, std::max(0, 255 - std::abs(v-128)*2)) + v/2;
            const int b = std::min(255, std::max(0, 255 - v*2)) + std::min(255, 255-v);
            dst[x] = qRgb(std::min(255,r/2), std::min(255,g/2), std::min(255,b/2));
        }
    }
    return mvcore::fromQImage(out);
}
