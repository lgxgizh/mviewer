// Bounded PNG display LOD tests (perf review P2-4). Exercises SourceImage::decodeLod
// on PNG fixtures written by QImage::save: RGB gradient, gray quadrants, straight
// alpha, indexed color, 16-bit RGB, odd sizes, truncated input, and a timing
// comparison against QImageReader::setScaledSize.

#include "core/image/SourceImage.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"

#include <QColorSpace>
#include <QElapsedTimer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageReader>
#include <QTemporaryDir>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <limits>
#include <string>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            std::printf("  PASS: %s\n", msg);                                                      \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::printf("  FAIL: %s\n", msg);                                                      \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)

namespace
{

QSize expectedLodSize(int w, int h, int maxEdge)
{
    const double ratio = static_cast<double>(maxEdge) / std::max(w, h);
    return QSize(std::max(1, static_cast<int>(std::lround(w * ratio))),
                 std::max(1, static_cast<int>(std::lround(h * ratio))));
}

void fillRgbGradient(QImage &img)
{
    const int w = img.width();
    const int h = img.height();
    const int xDen = std::max(1, w - 1);
    const int yDen = std::max(1, h - 1);
    for (int y = 0; y < h; ++y)
    {
        uint8_t *line = img.scanLine(y);
        for (int x = 0; x < w; ++x)
        {
            line[x * 3 + 0] = static_cast<uint8_t>(x * 255 / xDen);
            line[x * 3 + 1] = static_cast<uint8_t>(y * 255 / yDen);
            line[x * 3 + 2] = 128;
        }
    }
}

void fillRgbGradient16(QImage &img)
{
    const int w = img.width();
    const int h = img.height();
    const int xDen = std::max(1, w - 1);
    const int yDen = std::max(1, h - 1);
    for (int y = 0; y < h; ++y)
    {
        auto *line = reinterpret_cast<uint16_t *>(img.scanLine(y));
        for (int x = 0; x < w; ++x)
        {
            line[x * 4 + 0] = static_cast<uint16_t>(static_cast<uint32_t>(x) * 65535u /
                                                    static_cast<uint32_t>(xDen));
            line[x * 4 + 1] = static_cast<uint16_t>(static_cast<uint32_t>(y) * 65535u /
                                                    static_cast<uint32_t>(yDen));
            line[x * 4 + 2] = 32768;
            line[x * 4 + 3] = 65535;
        }
    }
}

// Tag sRGB so the fixture is not a gAMA/cHRM-only PNG. That combination is
// intentionally left on the QImageReader path.
bool savePng(QImage &img, const QString &path)
{
    img.setColorSpace(QColorSpace::SRgb);
    return img.save(path, "PNG");
}

const uint8_t *pixelAt(const ImageData &img, int x, int y)
{
    if (img.isNull() || x < 0 || y < 0 || x >= img.width || y >= img.height)
        return nullptr;
    if (!img.buffer || img.buffer->empty())
        return nullptr;
    const int cpp = img.channelsPerPixel();
    const size_t off = static_cast<size_t>(y) * static_cast<size_t>(img.stride()) +
                       static_cast<size_t>(x) * static_cast<size_t>(cpp);
    if (off + static_cast<size_t>(cpp) > img.buffer->size())
        return nullptr;
    return img.buffer->data() + off;
}

bool rgbNear(const ImageData &img, int x, int y, int r, int g, int b, int tol)
{
    const uint8_t *p = pixelAt(img, x, y);
    if (p == nullptr || img.format != PixelFormat::RGB24)
        return false;
    return std::abs(static_cast<int>(p[0]) - r) <= tol &&
           std::abs(static_cast<int>(p[1]) - g) <= tol &&
           std::abs(static_cast<int>(p[2]) - b) <= tol;
}

bool grayNear(const ImageData &img, int x, int y, int value, int tol)
{
    const uint8_t *p = pixelAt(img, x, y);
    if (p == nullptr || img.format != PixelFormat::Grayscale8)
        return false;
    return std::abs(static_cast<int>(p[0]) - value) <= tol;
}

double channelMad(const ImageData &got, const QImage &ref)
{
    const QImage rgb = ref.convertToFormat(QImage::Format_RGB888);
    if (got.isNull() || rgb.isNull() || got.width != rgb.width() || got.height != rgb.height())
        return 1e9;
    double sum[3] = {0, 0, 0};
    const double n = static_cast<double>(got.width) * static_cast<double>(got.height);
    if (n <= 0)
        return 1e9;
    for (int y = 0; y < got.height; ++y)
    {
        const uint8_t *sl = rgb.constScanLine(y);
        for (int x = 0; x < got.width; ++x)
        {
            const uint8_t *p = pixelAt(got, x, y);
            if (p == nullptr)
                return 1e9;
            sum[0] += std::abs(static_cast<int>(p[0]) - static_cast<int>(sl[x * 3 + 0]));
            sum[1] += std::abs(static_cast<int>(p[1]) - static_cast<int>(sl[x * 3 + 1]));
            sum[2] += std::abs(static_cast<int>(p[2]) - static_cast<int>(sl[x * 3 + 2]));
        }
    }
    return std::max(sum[0] / n, std::max(sum[1] / n, sum[2] / n));
}

bool cornersWithin(const ImageData &got, const QImage &ref, int tol)
{
    const QImage rgb = ref.convertToFormat(QImage::Format_RGB888);
    if (got.isNull() || rgb.isNull())
        return false;
    const int w = got.width;
    const int h = got.height;
    const uint8_t *tl = rgb.constScanLine(0);
    const uint8_t *br = rgb.constScanLine(h - 1) + (w - 1) * 3;
    return rgbNear(got, 0, 0, tl[0], tl[1], tl[2], tol) &&
           rgbNear(got, w - 1, h - 1, br[0], br[1], br[2], tol);
}

mviewer::core::SourceImage::RasterResult decodeLod(const std::string &path, int maxEdge)
{
    mviewer::core::SourceDecodeStats::instance().counters().reset();
    auto source = mviewer::core::SourceImage::open(path);
    if (!source)
        return {};
    return source->decodeLod(maxEdge);
}

} // namespace

static void testGradient(const QString &dir)
{
    const QString qpath = dir + "/grad_2048x1536.png";
    QImage img(2048, 1536, QImage::Format_RGB888);
    CHECK(!img.isNull(), "a: gradient QImage allocated");
    if (img.isNull())
        return;
    fillRgbGradient(img);
    CHECK(savePng(img, qpath), "a: gradient PNG saved");
    const std::string path = qpath.toStdString();

    const auto lod = decodeLod(path, 256);
    CHECK(lod.ok && !lod.pixels.isNull(), "a: decodeLod(256) ok");
    CHECK(lod.pixels.width == 256 && lod.pixels.height == 192, "a: pixels 256x192");
    CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod, "a: decodePath NativeLod");
    const auto &c = mviewer::core::SourceDecodeStats::instance().counters();
    CHECK(c.fullDecode.load() == 0 && c.fullDecodeScaled.load() == 0,
          "a: no fullDecode / fullDecodeScaled");

    const QImage loaded(qpath);
    const QImage ref = loaded.scaled(256, 192, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    CHECK(channelMad(lod.pixels, ref) < 4.0, "a: mean abs diff per channel < 4");
    CHECK(cornersWithin(lod.pixels, ref, 6), "a: top-left/bottom-right within 6");
}

static void testGray(const QString &dir)
{
    const QString qpath = dir + "/gray_1000x800.png";
    QImage img(1000, 800, QImage::Format_Grayscale8);
    CHECK(!img.isNull(), "b: gray QImage allocated");
    if (img.isNull())
        return;
    for (int y = 0; y < 800; ++y)
    {
        uint8_t *line = img.scanLine(y);
        for (int x = 0; x < 1000; ++x)
        {
            const uint8_t top = x < 500 ? 20 : 90;
            const uint8_t bottom = x < 500 ? 160 : 230;
            line[x] = y < 400 ? top : bottom;
        }
    }
    CHECK(savePng(img, qpath), "b: gray PNG saved");

    const auto lod = decodeLod(qpath.toStdString(), 100);
    CHECK(lod.ok && !lod.pixels.isNull(), "b: decodeLod(100) ok");
    CHECK(lod.pixels.format == PixelFormat::Grayscale8, "b: pixels Grayscale8");
    CHECK(grayNear(lod.pixels, 25, 20, 20, 2), "b: top-left quadrant 20");
    CHECK(grayNear(lod.pixels, 75, 20, 90, 2), "b: top-right quadrant 90");
    CHECK(grayNear(lod.pixels, 25, 60, 160, 2), "b: bottom-left quadrant 160");
    CHECK(grayNear(lod.pixels, 75, 60, 230, 2), "b: bottom-right quadrant 230");
}

static void testAlpha(const QString &dir)
{
    const QString qpath = dir + "/alpha_800x600.png";
    QImage img(800, 600, QImage::Format_ARGB32);
    CHECK(!img.isNull(), "c: alpha QImage allocated");
    if (img.isNull())
        return;
    for (int y = 0; y < 600; ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(img.scanLine(y));
        for (int x = 0; x < 800; ++x)
            line[x] = x < 400 ? qRgba(255, 0, 0, 255) : qRgba(0, 0, 0, 0);
    }
    CHECK(savePng(img, qpath), "c: alpha PNG saved");

    const auto lod = decodeLod(qpath.toStdString(), 80);
    CHECK(lod.ok && !lod.pixels.isNull(), "c: decodeLod(80) non-null");
    CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod, "c: decodePath NativeLod");
    const int x = std::max(0, lod.pixels.width / 4 - 1);
    const int y = lod.pixels.height / 2;
    CHECK(rgbNear(lod.pixels, x, y, 255, 0, 0, 2), "c: left-quarter pixel is red");
}

static void testIndexed(const QString &dir)
{
    const QString qpath = dir + "/indexed_1000x800.png";
    QImage img(1000, 800, QImage::Format_Indexed8);
    CHECK(!img.isNull(), "d: indexed QImage allocated");
    if (img.isNull())
        return;
    img.setColorCount(4);
    img.setColor(0, qRgb(220, 30, 30));
    img.setColor(1, qRgb(30, 220, 30));
    img.setColor(2, qRgb(30, 30, 220));
    img.setColor(3, qRgb(220, 220, 30));
    for (int y = 0; y < 800; ++y)
    {
        uint8_t *line = img.scanLine(y);
        for (int x = 0; x < 1000; ++x)
            line[x] = static_cast<uint8_t>((y >= 400 ? 2 : 0) + (x >= 500 ? 1 : 0));
    }
    CHECK(savePng(img, qpath), "d: indexed PNG saved");

    const auto lod = decodeLod(qpath.toStdString(), 100);
    CHECK(lod.ok && !lod.pixels.isNull(), "d: decodeLod(100) ok");
    CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod, "d: decodePath NativeLod");
    CHECK(rgbNear(lod.pixels, 25, 20, 220, 30, 30, 2), "d: top-left quadrant red");
    CHECK(rgbNear(lod.pixels, 75, 20, 30, 220, 30, 2), "d: top-right quadrant green");
    CHECK(rgbNear(lod.pixels, 25, 60, 30, 30, 220, 2), "d: bottom-left quadrant blue");
    CHECK(rgbNear(lod.pixels, 75, 60, 220, 220, 30, 2), "d: bottom-right quadrant yellow");
}

static void testRgb16(const QString &dir)
{
    const QString qpath = dir + "/rgb16_1200x900.png";
    QImage img(1200, 900, QImage::Format_RGBX64);
    CHECK(!img.isNull(), "e: 16-bit QImage allocated");
    if (img.isNull())
        return;
    fillRgbGradient16(img);
    CHECK(savePng(img, qpath), "e: 16-bit PNG saved");

    const auto lod = decodeLod(qpath.toStdString(), 120);
    CHECK(lod.ok && !lod.pixels.isNull(), "e: decodeLod(120) non-null");
    CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod, "e: decodePath NativeLod");
    const QSize exp = expectedLodSize(1200, 900, 120);
    const QImage loaded(qpath);
    const QImage ref = loaded.scaled(exp, Qt::IgnoreAspectRatio, Qt::SmoothTransformation);
    CHECK(channelMad(lod.pixels, ref) < 4.0, "e: mean abs diff vs RGB888 reference < 4");
}

static void testTruncated(const QString &dir)
{
    const QString srcPath = dir + "/grad_2048x1536.png";
    const QString dstPath = dir + "/truncated.png";
    QFile src(srcPath);
    CHECK(src.open(QIODevice::ReadOnly), "f: source PNG opened for truncate");
    const QByteArray data = src.readAll();
    src.close();
    QFile dst(dstPath);
    CHECK(dst.open(QIODevice::WriteOnly), "f: truncated PNG created");
    dst.write(data.left(data.size() / 2));
    dst.close();

    auto source = mviewer::core::SourceImage::open(dstPath.toStdString());
    if (!source)
    {
        std::printf("  INFO: truncated PNG: SourceImage::open failed\n");
    }
    else
    {
        const auto lod = source->decodeLod(256);
        std::printf("  INFO: truncated PNG: open ok, decodeLod ok=%d null=%d\n",
                    static_cast<int>(lod.ok), static_cast<int>(lod.pixels.isNull()));
    }
    CHECK(true, "f: truncated PNG did not crash");
}

static void testOddSizes(const QString &dir)
{
    {
        const QString qpath = dir + "/odd_1001x7.png";
        QImage img(1001, 7, QImage::Format_RGB888);
        fillRgbGradient(img);
        CHECK(savePng(img, qpath), "g: 1001x7 PNG saved");
        const auto lod = decodeLod(qpath.toStdString(), 64);
        const QSize exp = expectedLodSize(1001, 7, 64);
        CHECK(lod.ok && !lod.pixels.isNull(), "g: 1001x7 decodeLod non-null");
        CHECK(lod.pixels.width == exp.width() && lod.pixels.height == exp.height(),
              "g: 1001x7 matches lround rule");
    }
    {
        const QString qpath = dir + "/odd_7x1001.png";
        QImage img(7, 1001, QImage::Format_RGB888);
        fillRgbGradient(img);
        CHECK(savePng(img, qpath), "g: 7x1001 PNG saved");
        const auto lod = decodeLod(qpath.toStdString(), 64);
        const QSize exp = expectedLodSize(7, 1001, 64);
        CHECK(lod.ok && !lod.pixels.isNull(), "g: 7x1001 decodeLod non-null");
        CHECK(lod.pixels.width == exp.width() && lod.pixels.height == exp.height(),
              "g: 7x1001 matches lround rule");
    }
}

static void testTiming(const QString &dir)
{
    const QString qpath = dir + "/timing_4096.png";
    QImage img(4096, 4096, QImage::Format_RGB888);
    CHECK(!img.isNull(), "h: 4096x4096 QImage allocated");
    if (img.isNull())
        return;
    fillRgbGradient(img);
    CHECK(savePng(img, qpath), "h: 4096x4096 PNG saved");
    const std::string path = qpath.toStdString();

    QElapsedTimer timer;
    qint64 bestBounded = std::numeric_limits<qint64>::max();
    for (int i = 0; i < 3; ++i)
    {
        auto source = mviewer::core::SourceImage::open(path);
        timer.start();
        if (source)
            (void)source->decodeLod(256);
        bestBounded = std::min(bestBounded, timer.elapsed());
    }

    qint64 bestQt = std::numeric_limits<qint64>::max();
    for (int i = 0; i < 3; ++i)
    {
        timer.start();
        QImageReader reader(qpath);
        reader.setScaledSize(QSize(256, 256));
        (void)reader.read();
        bestQt = std::min(bestQt, timer.elapsed());
    }
    std::printf("png_lod timing: bounded=%lld ms qt_scaled=%lld ms\n",
                static_cast<long long>(bestBounded), static_cast<long long>(bestQt));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    if (!QtDecoder::hasBoundedPngLod())
    {
        std::printf("png_lod_tests: bundled libpng not available, SKIP\n");
        return 0;
    }
    DecoderRegistry::instance().resetToDefaults();

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temporary directory created");
    if (!dir.isValid())
    {
        std::printf("=== png_lod_tests: FAIL ===\n");
        return 1;
    }

    testGradient(dir.path());
    testGray(dir.path());
    testAlpha(dir.path());
    testIndexed(dir.path());
    testRgb16(dir.path());
    testTruncated(dir.path());
    testOddSizes(dir.path());
    testTiming(dir.path());

    std::printf("=== png_lod_tests: %s ===\n", g_fail == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
