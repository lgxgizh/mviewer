// JPEG fast path (review P1-05): libjpeg-turbo must agree with Qt's decoder.
//
// The point of this test is to pin CORRECTNESS before the fast path is wired
// into the browse flow: same geometry, same pixel meaning, and a bounded
// difference against Qt's own decode of the same file (two IDCT
// implementations differ by a couple of codes on a lossy codec). It also pins
// the fallback contract - a non-JPEG and an unsupported JPEG must come back
// null so the caller can use Qt.
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"
#include "core/image/decoder/JpegTurbo.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QTemporaryDir>

#include <cmath>
#include <cstdio>
#include <iostream>

namespace
{
int g_failures = 0;

void check(bool ok, const char *what, const char *file, int line)
{
    if (ok)
    {
        std::cout << "[ok] " << what << "\n";
        return;
    }
    std::cout << "[FAIL] " << what << " (" << file << ":" << line << ")\n";
    ++g_failures;
}
#define CHECK(cond, what) check((cond), (what), __FILE__, __LINE__)

// Mean absolute per-channel difference between two same-sized images.
double meanAbsDiff(const QImage &a, const QImage &b)
{
    if (a.size() != b.size() || a.isNull() || b.isNull())
        return 1e9;
    const QImage ca = a.convertToFormat(QImage::Format_RGB888);
    const QImage cb = b.convertToFormat(QImage::Format_RGB888);
    double total = 0.0;
    const int n = ca.width() * ca.height() * 3;
    for (int y = 0; y < ca.height(); ++y)
    {
        const uchar *ra = ca.constScanLine(y);
        const uchar *rb = cb.constScanLine(y);
        for (int i = 0; i < ca.width() * 3; ++i)
            total += std::abs(static_cast<int>(ra[i]) - static_cast<int>(rb[i]));
    }
    return total / static_cast<double>(n);
}

QImage makeFrame(int w, int h, bool gray, int seed)
{
    QImage img(w, h, gray ? QImage::Format_Grayscale8 : QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
    {
        for (int x = 0; x < w; ++x)
        {
            if (gray)
                img.setPixel(x, y, qRgb((x * 7 + y * 3 + seed) & 0xFF, 0, 0));
            else
                img.setPixel(x, y,
                             qRgb((x * 5 + seed) & 0xFF, (y * 9 + seed * 2) & 0xFF,
                                  ((x + y) * 3 + seed) & 0xFF));
        }
    }
    return img;
}

QString writeJpeg(const QString &dir, const QString &name, const QImage &img, int quality)
{
    const QString path = dir + QStringLiteral("/") + name;
    QImageWriter writer(path);
    writer.setFormat("JPEG");
    writer.setQuality(quality);
    if (!writer.write(img))
        return QString();
    return path;
}
} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    setbuf(stdout, nullptr);
    using mviewer::core::detail::decodeJpegTurbo;

    std::cout << "[info] libjpeg-turbo fast path: "
              << (mviewer::core::detail::hasTurboJpeg() ? "present" : "absent") << "\n";

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::cout << "jpeg_turbo_tests: FAIL (no temp dir)\n";
        return 1;
    }

    // ── baseline RGB JPEG ────────────────────────────────────────────────────
    {
        const QImage src = makeFrame(197, 131, false, 11);
        const QString path = writeJpeg(tmp.path(), QStringLiteral("rgb.jpg"), src, 92);
        CHECK(!path.isEmpty(), "RGB JPEG fixture written");

        QImageReader reader(path);
        const QImage qt = reader.read();
        CHECK(!qt.isNull(), "Qt decodes the fixture");

        const ImageData fast = decodeJpegTurbo(path.toUtf8().toStdString(), 0);
        CHECK(!fast.isNull(), "the fast path decodes a baseline RGB JPEG");
        CHECK(fast.width == qt.width() && fast.height == qt.height(),
              "fast path geometry matches Qt");

        const QImage fastQ = mvcore::toQImage(fast);
        const double diff = meanAbsDiff(qt, fastQ);
        CHECK(diff <= 2.0, "fast path pixels agree with Qt (mean abs diff <= 2)");
        std::cout << "[info] RGB mean abs diff = " << diff << "\n";
    }

    // ── greyscale JPEG ───────────────────────────────────────────────────────
    {
        const QImage src = makeFrame(128, 96, true, 5);
        const QString path = writeJpeg(tmp.path(), QStringLiteral("gray.jpg"), src, 90);
        CHECK(!path.isEmpty(), "greyscale JPEG fixture written");

        QImageReader reader(path);
        const QImage qt = reader.read();
        const ImageData fast = decodeJpegTurbo(path.toUtf8().toStdString(), 0);
        CHECK(!fast.isNull(), "the fast path decodes a greyscale JPEG");
        CHECK(fast.format == PixelFormat::Grayscale8, "a greyscale JPEG stays one byte per pixel");
        CHECK(fast.width == qt.width() && fast.height == qt.height(),
              "greyscale geometry matches Qt");
        const QImage fastQ = mvcore::toQImage(fast);
        CHECK(meanAbsDiff(qt, fastQ) <= 2.0, "greyscale pixels agree with Qt");
    }

    // ── scaled (LOD) decode ──────────────────────────────────────────────────
    {
        const QImage src = makeFrame(2048, 1024, false, 3);
        const QString path = writeJpeg(tmp.path(), QStringLiteral("big.jpg"), src, 88);
        CHECK(!path.isEmpty(), "large JPEG fixture written");
        const ImageData fast = decodeJpegTurbo(path.toUtf8().toStdString(), 512);
        CHECK(!fast.isNull(), "a scaled decode succeeds");
        const int edge = std::max(fast.width, fast.height);
        CHECK(edge <= 512, "the scaled decode honours maxEdge");
        CHECK(edge >= 256, "the scaled decode does not undershoot maxEdge");
        CHECK(fast.width == 512 && fast.height == 256,
              "the largest fitting factor (1/4) was chosen for a 2048x1024 source");
    }

    // ── fallback contract ────────────────────────────────────────────────────
    {
        const QString png = tmp.path() + QStringLiteral("/notjpeg.png");
        QImageWriter pngWriter(png);
        pngWriter.setFormat("PNG");
        CHECK(pngWriter.write(makeFrame(32, 32, false, 1)), "PNG fixture written");
        CHECK(decodeJpegTurbo(png.toUtf8().toStdString(), 0).isNull(),
              "a non-JPEG returns null so the caller falls back to Qt");
        CHECK(decodeJpegTurbo(QStringLiteral("/nonexistent/none.jpg").toUtf8().toStdString(), 0)
                  .isNull(),
              "a missing file returns null");
    }

    if (g_failures != 0)
    {
        std::cout << "jpeg_turbo_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "jpeg_turbo_tests: PASS\n";
    return 0;
}
