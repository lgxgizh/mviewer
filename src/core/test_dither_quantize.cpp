// NOLINTBEGIN
#include "core/image/DitherQuantize.h"
#include "core/image/decoder/QtDecoder.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QImageReader>
#include <QImageWriter>
#include <QString>
#include <QTemporaryDir>

#include <cstdint>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <vector>

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #cond);                            \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

namespace
{

using mviewer::core::ditherQImage16ToRgb888;
using mviewer::core::quantize16To8;

constexpr int kRampWidth = 256;
constexpr int kRampHeight = 8;

// Full-range ramp. Endpoints are exact quanta (0 and 65535). Interior samples
// sit 128 codes above each multiple of 257, so ordered dither has a remainder
// to distribute. A one-pixel-tall exact ramp cannot show more levels than
// `value >> 8` (both already use every 8-bit code); repeating each sample down
// the 8-row Bayer tile is where truncation stays flat and dither does not.
std::uint16_t rampSample(int x)
{
    if (x <= 0)
        return 0;
    if (x >= kRampWidth - 1)
        return 65535;
    return static_cast<std::uint16_t>(x * 257 + 128);
}

struct RampStats
{
    int ditherLevels = 0;
    int truncLevels = 0;
    int notTruncated = 0;
    double meanR = 0.0;
    double meanDiv = 0.0;
    bool tied = true;
    bool adjacent = true;
    bool endpoints = true;
};

RampStats measureRamp(const std::uint8_t *rgb, int width, int height, int stride)
{
    RampStats stats;
    double sumR = 0.0;
    double sumDiv = 0.0;
    const int count = width * height;
    for (int x = 0; x < width; ++x)
    {
        const std::uint16_t sample = rampSample(x);
        bool seen[256] = {};
        int unique = 0;
        int lo = 255;
        int hi = 0;
        for (int y = 0; y < height; ++y)
        {
            const std::size_t row = static_cast<std::size_t>(y) * static_cast<std::size_t>(stride);
            const std::size_t at = row + static_cast<std::size_t>(x) * 3u;
            const int r = rgb[at];
            const int g = rgb[at + 1];
            const int b = rgb[at + 2];
            if (g != r || b != r)
                stats.tied = false;
            if (!seen[r])
            {
                seen[r] = true;
                ++unique;
            }
            if (r < lo)
                lo = r;
            if (r > hi)
                hi = r;
            if (r != (sample >> 8))
                ++stats.notTruncated;
            sumR += r;
            sumDiv += static_cast<double>(sample / 257);
        }
        if (hi - lo > 1)
            stats.adjacent = false;
        stats.ditherLevels += unique;
        // `value >> 8` is constant down the column: one truncated level.
        stats.truncLevels += 1;
        if (x == 0 && (lo != 0 || hi != 0))
            stats.endpoints = false;
        if (x == width - 1 && (lo != 255 || hi != 255))
            stats.endpoints = false;
    }
    if (count > 0)
    {
        stats.meanR = sumR / static_cast<double>(count);
        stats.meanDiv = sumDiv / static_cast<double>(count);
    }
    return stats;
}

void checkRampStats(const RampStats &stats)
{
    const double delta =
        stats.meanR > stats.meanDiv ? stats.meanR - stats.meanDiv : stats.meanDiv - stats.meanR;
    CHECK(stats.tied);
    CHECK(stats.adjacent);
    CHECK(stats.endpoints);
    // Summed per-column unique R values versus the single high-byte each
    // column truncates to. Interior columns split into two adjacent levels.
    CHECK(stats.ditherLevels > stats.truncLevels + stats.truncLevels / 2);
    CHECK(stats.notTruncated > kRampWidth);
    CHECK(delta < 3.0);
}

std::vector<std::uint8_t> packRgb888(const QImage &image)
{
    const int w = image.width();
    const int h = image.height();
    std::vector<std::uint8_t> packed(static_cast<std::size_t>(w) * static_cast<std::size_t>(h) *
                                     3u);
    for (int y = 0; y < h; ++y)
    {
        const uchar *row = image.constScanLine(y);
        std::memcpy(packed.data() + static_cast<std::size_t>(y) * static_cast<std::size_t>(w) * 3u,
                    row, static_cast<std::size_t>(w) * 3u);
    }
    return packed;
}

QImage makeGrayRamp()
{
    QImage image(kRampWidth, kRampHeight, QImage::Format_Grayscale16);
    for (int y = 0; y < image.height(); ++y)
    {
        auto *row = reinterpret_cast<std::uint16_t *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            row[x] = rampSample(x);
    }
    return image;
}

void testExactQuanta()
{
    const int xs[] = {0, 1, 7, 8, 15, -1, -8, 100, std::numeric_limits<int>::min()};
    const int ys[] = {0, 3, 7, 9, -1, -5, 64, 2, std::numeric_limits<int>::min()};
    const int count = static_cast<int>(sizeof(xs) / sizeof(xs[0]));
    for (int i = 0; i < count; ++i)
    {
        CHECK(quantize16To8(0, xs[i], ys[i]) == 0);
        CHECK(quantize16To8(65535, xs[i], ys[i]) == 255);
    }
    const int positions[][2] = {{0, 0}, {3, 5}, {8, 1}, {-1, -3}, {1000, -9}};
    for (int n = 0; n <= 255; ++n)
    {
        const auto sample = static_cast<std::uint16_t>(n * 257);
        for (const auto &pos : positions)
            CHECK(quantize16To8(sample, pos[0], pos[1]) == static_cast<std::uint8_t>(n));
    }
}

void testMidQuantumTile()
{
    const auto mid = static_cast<std::uint16_t>(128 * 257 + 128);
    bool saw128 = false;
    bool saw129 = false;
    bool other = false;
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const std::uint8_t level = quantize16To8(mid, x, y);
            if (level == 128)
                saw128 = true;
            else if (level == 129)
                saw129 = true;
            else
                other = true;
        }
    }
    CHECK(saw128 && saw129 && !other);
    CHECK(quantize16To8(mid, 0, 0) == quantize16To8(mid, 8, 16));
    CHECK(quantize16To8(mid, 7, 7) == quantize16To8(mid, -1, -1));
    for (int y = 10; y < 18; ++y)
    {
        for (int x = -3; x <= 4; ++x)
        {
            const std::uint8_t level = quantize16To8(mid, x, y);
            CHECK(level == 128 || level == 129);
        }
    }
    const std::uint8_t atMin =
        quantize16To8(mid, std::numeric_limits<int>::min(), std::numeric_limits<int>::min());
    CHECK(atMin == 128 || atMin == 129);
}

void testFlatField()
{
    QImage image(8, 8, QImage::Format_Grayscale16);
    const auto sample = static_cast<std::uint16_t>(128 * 257);
    for (int y = 0; y < image.height(); ++y)
    {
        auto *row = reinterpret_cast<std::uint16_t *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            row[x] = sample;
    }
    QImage out;
    CHECK(ditherQImage16ToRgb888(image, out));
    CHECK(out.format() == QImage::Format_RGB888);
    CHECK(out.width() == 8 && out.height() == 8);
    for (int y = 0; y < out.height(); ++y)
    {
        const uchar *row = out.constScanLine(y);
        for (int x = 0; x < out.width(); ++x)
        {
            CHECK(row[x * 3 + 0] == 128);
            CHECK(row[x * 3 + 1] == 128);
            CHECK(row[x * 3 + 2] == 128);
        }
    }
}

void testRgb64Channels(QImage::Format format)
{
    QImage image(8, 8, format);
    const auto mid = static_cast<std::uint16_t>(128 * 257 + 128);
    const auto g = static_cast<std::uint16_t>(10 * 257);
    const auto b = static_cast<std::uint16_t>(40 * 257);
    for (int y = 0; y < image.height(); ++y)
    {
        auto *row = reinterpret_cast<std::uint16_t *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
        {
            row[x * 4 + 0] = mid;
            row[x * 4 + 1] = g;
            row[x * 4 + 2] = b;
            row[x * 4 + 3] = 7;
        }
    }
    QImage sentinel(2, 2, QImage::Format_RGB888);
    sentinel.fill(qRgb(1, 2, 3));
    QImage out = sentinel;
    CHECK(ditherQImage16ToRgb888(image, out));
    CHECK(out.format() == QImage::Format_RGB888);
    CHECK(out.width() == 8 && out.height() == 8);
    bool saw128 = false;
    bool saw129 = false;
    bool other = false;
    for (int y = 0; y < out.height(); ++y)
    {
        const uchar *row = out.constScanLine(y);
        for (int x = 0; x < out.width(); ++x)
        {
            const int r = row[x * 3];
            CHECK(row[x * 3 + 1] == 10);
            CHECK(row[x * 3 + 2] == 40);
            if (r == 128)
                saw128 = true;
            else if (r == 129)
                saw129 = true;
            else
                other = true;
        }
    }
    CHECK(saw128 && saw129 && !other);
}

bool sentinelUntouched(const QImage &image)
{
    if (image.width() != 3 || image.height() != 2)
        return false;
    const QRgb expect = qRgb(9, 8, 7);
    for (int y = 0; y < image.height(); ++y)
    {
        for (int x = 0; x < image.width(); ++x)
        {
            if (image.pixel(x, y) != expect)
                return false;
        }
    }
    return true;
}

void testRejectLeavesOutput()
{
    QImage sentinel(3, 2, QImage::Format_RGB888);
    sentinel.fill(qRgb(9, 8, 7));
    QImage eight(4, 4, QImage::Format_RGB888);
    eight.fill(qRgb(4, 5, 6));
    CHECK(!ditherQImage16ToRgb888(eight, sentinel));
    CHECK(sentinelUntouched(sentinel));
    QImage gray8(4, 4, QImage::Format_Grayscale8);
    gray8.fill(12);
    CHECK(!ditherQImage16ToRgb888(gray8, sentinel));
    CHECK(sentinelUntouched(sentinel));
    QImage prem(4, 4, QImage::Format_RGBA64_Premultiplied);
    CHECK(!ditherQImage16ToRgb888(prem, sentinel));
    CHECK(sentinelUntouched(sentinel));
    QImage empty;
    CHECK(!ditherQImage16ToRgb888(empty, sentinel));
    CHECK(sentinelUntouched(sentinel));
}

void testGrayRampImage()
{
    const QImage src = makeGrayRamp();
    QImage out(1, 1, QImage::Format_RGB888);
    out.fill(qRgb(4, 4, 4));
    CHECK(ditherQImage16ToRgb888(src, out));
    CHECK(out.format() == QImage::Format_RGB888);
    CHECK(out.width() == kRampWidth && out.height() == kRampHeight);
    const std::vector<std::uint8_t> packed = packRgb888(out);
    checkRampStats(measureRamp(packed.data(), kRampWidth, kRampHeight, kRampWidth * 3));
}

bool writePng(const QImage &image, const QString &path)
{
    QImageWriter writer(path, "png");
    return writer.write(image);
}

std::string utf8Path(const QString &path)
{
    const QByteArray bytes = path.toUtf8();
    return std::string(bytes.constData(), static_cast<std::size_t>(bytes.size()));
}

void testPngRoundTrip()
{
    QTemporaryDir dir;
    CHECK(dir.isValid());
    if (!dir.isValid())
        return;

    const QString path16 = dir.filePath("ramp16.png");
    const QImage ramp = makeGrayRamp();
    CHECK(writePng(ramp, path16));
    {
        QImageReader probe(path16);
        const QImage probed = probe.read();
        const QImage::Format fmt = probed.format();
        const bool sixteen = fmt == QImage::Format_Grayscale16 || fmt == QImage::Format_RGBX64 ||
                             fmt == QImage::Format_RGBA64;
        CHECK(sixteen);
        if (fmt == QImage::Format_Grayscale16)
        {
            bool samplesMatch = probed.width() == kRampWidth && probed.height() == kRampHeight;
            for (int y = 0; samplesMatch && y < probed.height(); ++y)
            {
                const auto *row = reinterpret_cast<const std::uint16_t *>(probed.constScanLine(y));
                for (int x = 0; x < probed.width(); ++x)
                    samplesMatch = samplesMatch && row[x] == rampSample(x);
            }
            CHECK(samplesMatch);
        }
    }

    QtDecoder decoder;
    const ImageData decoded = decoder.decodeFull(utf8Path(path16));
    CHECK(!decoded.isNull());
    CHECK(decoded.format == PixelFormat::RGB24);
    CHECK(decoded.width == kRampWidth && decoded.height == kRampHeight);
    if (!decoded.isNull() && decoded.buffer && decoded.width == kRampWidth &&
        decoded.height == kRampHeight)
    {
        checkRampStats(
            measureRamp(decoded.buffer->data(), decoded.width, decoded.height, decoded.width * 3));
    }

    QImage eight(32, 4, QImage::Format_RGB888);
    std::vector<std::uint8_t> expected(static_cast<std::size_t>(eight.width()) *
                                       static_cast<std::size_t>(eight.height()) * 3u);
    for (int y = 0; y < eight.height(); ++y)
    {
        uchar *row = eight.scanLine(y);
        for (int x = 0; x < eight.width(); ++x)
        {
            const auto r = static_cast<std::uint8_t>(x * 17 + y);
            const auto g = static_cast<std::uint8_t>(200 - x);
            const auto b = static_cast<std::uint8_t>(x * 3 + y * 9);
            row[x * 3 + 0] = r;
            row[x * 3 + 1] = g;
            row[x * 3 + 2] = b;
            const std::size_t at =
                (static_cast<std::size_t>(y) * static_cast<std::size_t>(eight.width()) +
                 static_cast<std::size_t>(x)) *
                3u;
            expected[at] = r;
            expected[at + 1] = g;
            expected[at + 2] = b;
        }
    }
    const QString path8 = dir.filePath("plain8.png");
    CHECK(writePng(eight, path8));
    const ImageData decoded8 = decoder.decodeFull(utf8Path(path8));
    CHECK(!decoded8.isNull());
    CHECK(decoded8.format == PixelFormat::RGB24);
    CHECK(decoded8.width == eight.width() && decoded8.height == eight.height());
    CHECK(decoded8.buffer && decoded8.buffer->size() == expected.size());
    if (decoded8.buffer && decoded8.buffer->size() == expected.size())
        CHECK(std::memcmp(decoded8.buffer->data(), expected.data(), expected.size()) == 0);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testExactQuanta();
    testMidQuantumTile();
    testFlatField();
    testRgb64Channels(QImage::Format_RGBX64);
    testRgb64Channels(QImage::Format_RGBA64);
    testRejectLeavesOutput();
    testGrayRampImage();
    testPngRoundTrip();
    if (g_failures != 0)
    {
        std::printf("test_dither_quantize: %d failed\n", g_failures);
        return 1;
    }
    std::printf("test_dither_quantize: all passed\n");
    return 0;
}
// NOLINTEND
