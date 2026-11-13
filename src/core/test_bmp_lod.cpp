// Bounded BMP display LOD tests (perf review P2-4). Exercises SourceImage::decodeLod
// on uncompressed BMP fixtures: RGB gradient, indexed, top-down, odd sizes, truncated
// input, and a timing comparison against QImageReader::setScaledSize.

#include "core/image/SourceImage.h"
#include "core/image/decoder/DecoderRegistry.h"

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
#include <vector>

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

namespace mviewer::core::detail
{
// Defined in QtDecoder_bmp.cpp (exported from mviewer_core).
bool bmpBoxLodEligible(const std::string &path, QSize *size);
QImage decodeBmpBoxLod(const std::string &path, const QSize &target);
} // namespace mviewer::core::detail

namespace
{

QSize expectedLodSize(int w, int h, int maxEdge)
{
    const double ratio = static_cast<double>(maxEdge) / std::max(w, h);
    return QSize(std::max(1, static_cast<int>(std::lround(w * ratio))),
                 std::max(1, static_cast<int>(std::lround(h * ratio))));
}

void putU16(QByteArray &b, quint16 v)
{
    b.append(static_cast<char>(v & 0xff));
    b.append(static_cast<char>((v >> 8) & 0xff));
}

void putU32(QByteArray &b, quint32 v)
{
    b.append(static_cast<char>(v & 0xff));
    b.append(static_cast<char>((v >> 8) & 0xff));
    b.append(static_cast<char>((v >> 16) & 0xff));
    b.append(static_cast<char>((v >> 24) & 0xff));
}

void putI32(QByteArray &b, qint32 v)
{
    putU32(b, static_cast<quint32>(v));
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

bool saveBmp(const QImage &img, const QString &path)
{
    return img.save(path, "BMP");
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
    const uint8_t *br = rgb.constScanLine(h - 1) + static_cast<ptrdiff_t>(w - 1) * 3;
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

bool writeTopDownBmp(const QString &path, int w, int h)
{
    const int stride = ((w * 24 + 31) / 32) * 4;
    const quint32 pixelBytes = static_cast<quint32>(stride) * static_cast<quint32>(h);
    QByteArray hdr;
    hdr.append("BM", 2);
    putU32(hdr, 14u + 40u + pixelBytes);
    putU16(hdr, 0);
    putU16(hdr, 0);
    putU32(hdr, 54);
    putU32(hdr, 40);
    putI32(hdr, w);
    putI32(hdr, -h);
    putU16(hdr, 1);
    putU16(hdr, 24);
    putU32(hdr, 0);
    putU32(hdr, pixelBytes);
    putI32(hdr, 0);
    putI32(hdr, 0);
    putU32(hdr, 0);
    putU32(hdr, 0);

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return false;
    if (f.write(hdr) != hdr.size())
        return false;
    QByteArray row(stride, '\0');
    const int half = h / 2;
    for (int y = 0; y < h; ++y)
    {
        const bool top = y < half;
        for (int x = 0; x < w; ++x)
        {
            row[x * 3 + 0] = static_cast<char>(top ? 0 : 255); // B
            row[x * 3 + 1] = 0;                                // G
            row[x * 3 + 2] = static_cast<char>(top ? 255 : 0); // R
        }
        if (f.write(row) != row.size())
            return false;
    }
    return true;
}

} // namespace

static void testGradient(const QString &dir)
{
    const QString qpath = dir + "/grad_2048x1536.bmp";
    QImage img(2048, 1536, QImage::Format_RGB888);
    CHECK(!img.isNull(), "a: gradient QImage allocated");
    if (img.isNull())
        return;
    fillRgbGradient(img);
    CHECK(saveBmp(img, qpath), "a: gradient BMP saved");
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

static void testIndexed(const QString &dir)
{
    const QString qpath = dir + "/indexed_1000x800.bmp";
    QImage img(1000, 800, QImage::Format_Indexed8);
    CHECK(!img.isNull(), "b: indexed QImage allocated");
    if (img.isNull())
        return;
    // Qt writes palettes of <= 16 colours as 4 bpp (not eligible); use 256.
    img.setColorCount(256);
    for (int i = 4; i < 256; ++i)
        img.setColor(i, qRgb(i, i, i));
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
    CHECK(saveBmp(img, qpath), "b: indexed BMP saved");

    const auto lod = decodeLod(qpath.toStdString(), 100);
    CHECK(lod.ok && !lod.pixels.isNull(), "b: decodeLod(100) ok");
    CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod, "b: decodePath NativeLod");
    CHECK(rgbNear(lod.pixels, 25, 20, 220, 30, 30, 2), "b: top-left quadrant red");
    CHECK(rgbNear(lod.pixels, 75, 20, 30, 220, 30, 2), "b: top-right quadrant green");
    CHECK(rgbNear(lod.pixels, 25, 60, 30, 30, 220, 2), "b: bottom-left quadrant blue");
    CHECK(rgbNear(lod.pixels, 75, 60, 220, 220, 30, 2), "b: bottom-right quadrant yellow");
}

static void testTopDown(const QString &dir)
{
    // Qt's BMP handler reports a negative height for top-down files, so
    // SourceImage::open rejects them; exercise the bounded decoder directly.
    const QString qpath = dir + "/topdown_640x480.bmp";
    CHECK(writeTopDownBmp(qpath, 640, 480), "c: top-down BMP written");
    const std::string path = qpath.toStdString();
    QSize size;
    CHECK(mviewer::core::detail::bmpBoxLodEligible(path, &size), "c: top-down BMP eligible");
    CHECK(size == QSize(640, 480), "c: eligible size is 640x480");
    const QImage lod = mviewer::core::detail::decodeBmpBoxLod(path, QSize(64, 48));
    CHECK(!lod.isNull() && lod.size() == QSize(64, 48), "c: decodeBmpBoxLod 64x48");
    if (lod.isNull())
        return;
    CHECK(lod.pixel(0, 0) == qRgb(255, 0, 0), "c: top row red");
    CHECK(lod.pixel(0, 47) == qRgb(0, 0, 255), "c: bottom row blue");
}

static void testOddSizes(const QString &dir)
{
    {
        const QString qpath = dir + "/odd_1001x7.bmp";
        QImage img(1001, 7, QImage::Format_RGB888);
        fillRgbGradient(img);
        CHECK(saveBmp(img, qpath), "d: 1001x7 BMP saved");
        const auto lod = decodeLod(qpath.toStdString(), 64);
        const QSize exp = expectedLodSize(1001, 7, 64);
        CHECK(lod.ok && !lod.pixels.isNull(), "d: 1001x7 decodeLod non-null");
        CHECK(lod.pixels.width == exp.width() && lod.pixels.height == exp.height(),
              "d: 1001x7 matches lround rule");
    }
    {
        const QString qpath = dir + "/odd_7x1001.bmp";
        QImage img(7, 1001, QImage::Format_RGB888);
        fillRgbGradient(img);
        CHECK(saveBmp(img, qpath), "d: 7x1001 BMP saved");
        const auto lod = decodeLod(qpath.toStdString(), 64);
        const QSize exp = expectedLodSize(7, 1001, 64);
        CHECK(lod.ok && !lod.pixels.isNull(), "d: 7x1001 decodeLod non-null");
        CHECK(lod.pixels.width == exp.width() && lod.pixels.height == exp.height(),
              "d: 7x1001 matches lround rule");
    }
}

static void testTruncated(const QString &dir)
{
    const QString srcPath = dir + "/grad_2048x1536.bmp";
    const QString dstPath = dir + "/truncated.bmp";
    QFile src(srcPath);
    CHECK(src.open(QIODevice::ReadOnly), "e: source BMP opened for truncate");
    const QByteArray data = src.readAll();
    src.close();
    QFile dst(dstPath);
    CHECK(dst.open(QIODevice::WriteOnly), "e: truncated BMP created");
    dst.write(data.left(data.size() / 2));
    dst.close();

    auto source = mviewer::core::SourceImage::open(dstPath.toStdString());
    if (!source)
    {
        std::printf("  INFO: truncated BMP: SourceImage::open failed\n");
    }
    else
    {
        const auto lod = source->decodeLod(256);
        std::printf("  INFO: truncated BMP: open ok, decodeLod ok=%d null=%d\n",
                    static_cast<int>(lod.ok), static_cast<int>(lod.pixels.isNull()));
    }
    CHECK(true, "e: truncated BMP did not crash");
}

static void testTiming(const QString &dir)
{
    const QString qpath = dir + "/timing_4096.bmp";
    QImage img(4096, 4096, QImage::Format_RGB888);
    CHECK(!img.isNull(), "g: 4096x4096 QImage allocated");
    if (img.isNull())
        return;
    fillRgbGradient(img);
    CHECK(saveBmp(img, qpath), "g: 4096x4096 BMP saved");
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
    std::printf("bmp_lod timing: bounded=%lld ms qt_scaled=%lld ms\n",
                static_cast<long long>(bestBounded), static_cast<long long>(bestQt));
}

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    DecoderRegistry::instance().resetToDefaults();

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temporary directory created");
    if (!dir.isValid())
    {
        std::printf("=== bmp_lod_tests: FAIL ===\n");
        return 1;
    }

    testGradient(dir.path());
    testIndexed(dir.path());
    testTopDown(dir.path());
    testOddSizes(dir.path());
    testTruncated(dir.path());
    testTiming(dir.path());

    std::printf("=== bmp_lod_tests: %s ===\n", g_fail == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
