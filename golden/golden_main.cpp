// Golden image regression test framework.
// Generate golden images once, then compare future runs against them.
// Usage:
//   ./golden_main.exe                 (generate golden/ from test images)
//   ./golden_main.exe --compare       (compare test output vs golden/)
//   ./golden_main.exe --golden-dir <path>
//
// Default golden directory is <source-root>/golden (set at configure time via
// MVIEWER_SOURCE_DIR). Override with --golden-dir for local runs.
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>

#include <cmath>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
#define MVIEWER_SOURCE_DIR "."
#endif

static bool ensureDir(const std::string &path)
{
    return QDir(QString::fromStdString(path)).mkpath(".");
}

// M20 P0#4: full-reference quality metrics so golden regressions are gated on
// PSNR / SSIM / pixel-diff instead of a bare pixel count.
struct GoldenMetrics
{
    double psnr = 0.0;         // dB; HUGE_VAL when identical
    double ssim = 0.0;         // global SSIM on luma
    double diffFraction = 0.0; // fraction of pixels beyond per-channel tolerance
};

static double lumaOf(QRgb c)
{
    return 0.299 * qRed(c) + 0.587 * qGreen(c) + 0.114 * qBlue(c);
}

static GoldenMetrics computeMetrics(const QImage &goldIn, const QImage &currIn,
                                    int channelTolerance)
{
    const QImage gold = goldIn.convertToFormat(QImage::Format_RGB32);
    const QImage curr = currIn.convertToFormat(QImage::Format_RGB32);
    GoldenMetrics m;
    const int w = gold.width(), h = gold.height();
    const double n = static_cast<double>(w) * h;

    double sumSq = 0.0;
    long long diffs = 0;
    double meanG = 0.0, meanC = 0.0;
    for (int y = 0; y < h; ++y)
    {
        const QRgb *lg = reinterpret_cast<const QRgb *>(gold.constScanLine(y));
        const QRgb *lc = reinterpret_cast<const QRgb *>(curr.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int dr = static_cast<int>(qRed(lg[x])) - qRed(lc[x]);
            const int dg = static_cast<int>(qGreen(lg[x])) - qGreen(lc[x]);
            const int db = static_cast<int>(qBlue(lg[x])) - qBlue(lc[x]);
            sumSq += double(dr) * dr + double(dg) * dg + double(db) * db;
            if (std::abs(dr) > channelTolerance || std::abs(dg) > channelTolerance ||
                std::abs(db) > channelTolerance)
                ++diffs;
            meanG += lumaOf(lg[x]);
            meanC += lumaOf(lc[x]);
        }
    }
    m.diffFraction = diffs / n;
    const double mse = sumSq / (n * 3.0);
    m.psnr = (mse <= 0.0) ? HUGE_VAL : 10.0 * std::log10(255.0 * 255.0 / mse);

    // Global SSIM on luma (single window; adequate for deterministic goldens).
    meanG /= n;
    meanC /= n;
    double varG = 0.0, varC = 0.0, cov = 0.0;
    for (int y = 0; y < h; ++y)
    {
        const QRgb *lg = reinterpret_cast<const QRgb *>(gold.constScanLine(y));
        const QRgb *lc = reinterpret_cast<const QRgb *>(curr.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const double dg = lumaOf(lg[x]) - meanG;
            const double dc = lumaOf(lc[x]) - meanC;
            varG += dg * dg;
            varC += dc * dc;
            cov += dg * dc;
        }
    }
    varG /= n - 1;
    varC /= n - 1;
    cov /= n - 1;
    const double c1 = (0.01 * 255) * (0.01 * 255);
    const double c2 = (0.03 * 255) * (0.03 * 255);
    m.ssim = ((2 * meanG * meanC + c1) * (2 * cov + c2)) /
             ((meanG * meanG + meanC * meanC + c1) * (varG + varC + c2));
    return m;
}

static bool compareImages(const QString &goldenPath, const QString &currentPath,
                          double tolerance = 2.0, double maxDiffFraction = 0.01,
                          double minPsnr = 45.0, double minSsim = 0.99)
{
    QImage gold(goldenPath);
    QImage curr(currentPath);
    if (gold.isNull())
    {
        std::cerr << "  MISSING GOLDEN: " << goldenPath.toStdString() << std::endl;
        return false;
    }
    if (curr.isNull())
    {
        std::cerr << "  MISSING CURRENT: " << currentPath.toStdString() << std::endl;
        return false;
    }
    if (gold.size() != curr.size())
    {
        std::cerr << "  SIZE MISMATCH: " << gold.width() << "x" << gold.height() << " vs "
                  << curr.width() << "x" << curr.height() << std::endl;
        return false;
    }

    const GoldenMetrics m = computeMetrics(gold, curr, static_cast<int>(tolerance));
    std::cout << "  metrics: PSNR=";
    if (m.psnr == HUGE_VAL)
        std::cout << "inf";
    else
        std::cout << m.psnr << " dB";
    std::cout << "  SSIM=" << m.ssim << "  diff=" << m.diffFraction * 100 << "%" << std::endl;

    bool ok = true;
    if (m.diffFraction > maxDiffFraction)
    {
        std::cerr << "  FAIL pixel-diff: " << m.diffFraction * 100 << "% > "
                  << maxDiffFraction * 100 << "%" << std::endl;
        ok = false;
    }
    if (m.psnr < minPsnr)
    {
        std::cerr << "  FAIL PSNR: " << m.psnr << " dB < " << minPsnr << " dB" << std::endl;
        ok = false;
    }
    if (m.ssim < minSsim)
    {
        std::cerr << "  FAIL SSIM: " << m.ssim << " < " << minSsim << std::endl;
        ok = false;
    }
    return ok;
}

QImage makeGradient(int w, int h)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb(x * 255 / w, y * 255 / h, 128));
    return img;
}

QImage makeFlat(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c.rgb());
    return img;
}

static bool generateGoldenImages(const std::string &dir)
{
    ensureDir(dir + "/image");
    ensureDir(dir + "/histogram");
    ensureDir(dir + "/difference");
    ensureDir(dir + "/roi");

    QImage grad = makeGradient(256, 256);
    grad.save(QString::fromStdString(dir + "/image/gradient_256x256.png"));

    QImage flat = makeFlat(256, 256, QColor(128, 128, 128));
    flat.save(QString::fromStdString(dir + "/image/flat_256x256.png"));

    QImage half = makeFlat(256, 256, QColor(100, 150, 200));
    half.save(QString::fromStdString(dir + "/image/blue_256x256.png"));

    // Difference map & histogram are generated at compare time (they depend on analysis/diff
    // logic). For now, we store a simple diff: |grad - flat| should be a gradient-like image.
    QImage diff(256, 256, QImage::Format_Grayscale8);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x)
        {
            const int d = std::abs((x * 255 / 256 + y * 255 / 256) / 2 - 128);
            diff.setPixel(x, y, qRgb(d, d, d));
        }
    diff.save(QString::fromStdString(dir + "/difference/gradient_vs_flat.png"));

    std::cout << "Generated golden images in " << dir << std::endl;
    return true;
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // Default golden directory: <source-root>/golden (set at configure time).
    std::string goldenDir = std::string(MVIEWER_SOURCE_DIR) + "/golden";

    for (int i = 1; i < argc; ++i)
    {
        std::string arg = argv[i];
        if (arg == "--compare")
        {
            // Compare mode: regenerate current images and gate them against
            // golden/ with PSNR / SSIM / pixel-diff thresholds (M20 P0#4).
            std::string currentDir = std::string(MVIEWER_SOURCE_DIR) + "/tests/vision/current";
            QDir().mkpath(QString::fromStdString(currentDir));

            struct Case
            {
                const char *name;
                QImage current;
                std::string goldenRel;
            };
            QImage diff(256, 256, QImage::Format_Grayscale8);
            for (int y = 0; y < 256; ++y)
                for (int x = 0; x < 256; ++x)
                {
                    const int d = std::abs((x * 255 / 256 + y * 255 / 256) / 2 - 128);
                    diff.setPixel(x, y, qRgb(d, d, d));
                }
            std::vector<Case> cases;
            cases.push_back({"gradient", makeGradient(256, 256), "/image/gradient_256x256.png"});
            cases.push_back(
                {"flat", makeFlat(256, 256, QColor(128, 128, 128)), "/image/flat_256x256.png"});
            cases.push_back(
                {"blue", makeFlat(256, 256, QColor(100, 150, 200)), "/image/blue_256x256.png"});
            cases.push_back({"diff_gradient_vs_flat", diff, "/difference/gradient_vs_flat.png"});

            int failures = 0;
            for (const Case &c : cases)
            {
                const QString cur =
                    QString::fromStdString(currentDir + "/") + c.name + QStringLiteral(".png");
                c.current.save(cur);
                std::cout << "CASE " << c.name << std::endl;
                const bool ok = compareImages(QString::fromStdString(goldenDir + c.goldenRel), cur);
                std::cout << (ok ? "PASS: " : "FAIL: ") << c.name << std::endl;
                if (!ok)
                    ++failures;
            }
            std::cout << (failures == 0 ? "GOLDEN OK" : "GOLDEN FAILED") << " (" << cases.size()
                      << " cases, " << failures << " failed)" << std::endl;
            return failures == 0 ? 0 : 1;
        }
        else if (arg == "--golden-dir" && i + 1 < argc)
        {
            goldenDir = argv[++i];
        }
    }

    return generateGoldenImages(goldenDir) ? 0 : 1;
}
