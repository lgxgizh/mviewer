// Golden image regression test framework.
// Generate golden images once, then compare future runs against them.
// Usage: 
//   ./golden_generate.exe   (generate golden/ from test images)
//   ./golden_compare.exe    (compare test output vs golden/)
#include <QImage>
#include <QDir>
#include <QFileInfo>
#include <QElapsedTimer>
#include <QCoreApplication>

#include <iostream>
#include <fstream>
#include <vector>
#include <string>
#include <cmath>

static std::string g_goldenDir;

static bool ensureDir(const std::string& path) {
    return QDir(QString::fromStdString(path)).mkpath(".");
}

static bool compareImages(const QString& goldenPath, const QString& currentPath,
                          double tolerance = 2.0, double maxDiffFraction = 0.01) {
    QImage gold(goldenPath);
    QImage curr(currentPath);
    if (gold.isNull()) { std::cerr << "  MISSING GOLDEN: " << goldenPath.toStdString() << std::endl; return false; }
    if (curr.isNull()) { std::cerr << "  MISSING CURRENT: " << currentPath.toStdString() << std::endl; return false; }
    if (gold.size() != curr.size()) { std::cerr << "  SIZE MISMATCH: " << gold.width() << "x" << gold.height()
                                                  << " vs " << curr.width() << "x" << curr.height() << std::endl; return false; }
    int diffs = 0;
    const int w = gold.width(), h = gold.height();
    for (int y = 0; y < h; ++y) {
        const QRgb* lg = reinterpret_cast<const QRgb*>(gold.constScanLine(y));
        const QRgb* lc = reinterpret_cast<const QRgb*>(curr.constScanLine(y));
        for (int x = 0; x < w; ++x) {
            const int dr = std::abs(static_cast<int>(qRed(lg[x])) - qRed(lc[x]));
            const int dg = std::abs(static_cast<int>(qGreen(lg[x])) - qGreen(lc[x]));
            const int db = std::abs(static_cast<int>(qBlue(lg[x])) - qBlue(lc[x]));
            if (dr > tolerance || dg > tolerance || db > tolerance) ++diffs;
        }
    }
    const double frac = static_cast<double>(diffs) / (w * h);
    if (frac > maxDiffFraction) {
        std::cerr << "  DIFF: " << diffs << " px (" << frac*100 << "% > " << maxDiffFraction*100 << "%)"
                  << std::endl;
        return false;
    }
    return true;
}

QImage makeGradient(int w, int h) {
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, qRgb(x*255/w, y*255/h, 128));
    return img;
}

QImage makeFlat(int w, int h, QColor c) {
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c.rgb());
    return img;
}

static bool generateGoldenImages(const std::string& dir) {
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

    // Difference map & histogram are generated at compare time (they depend on analysis/diff logic).
    // For now, we store a simple diff: |grad - flat| should be a gradient-like image.
    QImage diff(256, 256, QImage::Format_Grayscale8);
    for (int y = 0; y < 256; ++y)
        for (int x = 0; x < 256; ++x) {
            const int d = std::abs((x*255/256 + y*255/256)/2 - 128);
            diff.setPixel(x, y, qRgb(d, d, d));
        }
    diff.save(QString::fromStdString(dir + "/difference/gradient_vs_flat.png"));

    std::cout << "Generated golden images in " << dir << std::endl;
    return true;
}

int main(int argc, char** argv) {
    QCoreApplication app(argc, argv);
    g_goldenDir = "D:/mviewer/golden";

    if (argc > 1 && std::string(argv[1]) == "--compare") {
        // Compare mode: generate current images and compare vs golden/.
        ensureDir("D:/mviewer/tests/vision/current");
        QImage grad = makeGradient(256, 256);
        grad.save("D:/mviewer/tests/vision/current/gradient_256x256.png");
        bool ok = compareImages(
            QString::fromStdString(g_goldenDir + "/image/gradient_256x256.png"),
            "D:/mviewer/tests/vision/current/gradient_256x256.png");
        std::cout << (ok ? "PASS: gradient" : "FAIL: gradient") << std::endl;
        return ok ? 0 : 1;
    }

    return generateGoldenImages(g_goldenDir) ? 0 : 1;
}
