// Zoomed-in JPEG tiles are sliced from one decoded 1024 px block instead of a
// clipped decode per 256 px tile. Pixels must match the per-tile decode
// exactly, and one block must be decoded once for all of its tiles.
// Optional bench: MVIEWER_BENCH_JPEG=/path/to/100mp.jpg test_jpeg_block_tiles
// NOLINTBEGIN
#include "core/image/SourceImage.h"
#include "core/render/JpegBlockTiles.h"
#include "core/render/TileSourceDecode.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (c)                                                                                     \
            ++g_pass;                                                                              \
        else                                                                                       \
        {                                                                                          \
            ++g_fail;                                                                              \
            printf("FAIL: %s\n", m);                                                               \
        }                                                                                          \
    } while (0)

using mviewer::core::SourceImage;

static ImageData directTile(const std::string &path, int x, int y, int w, int h)
{
    auto source = SourceImage::open(path);
    if (!source || !source->isValid())
        return {};
    return mviewer::core::decodeTileFromSource(*source, ImageData{}, x, y, w, h, w, h);
}

static bool samePixels(const ImageData &a, const ImageData &b)
{
    return !a.isNull() && !b.isNull() && a.width == b.width && a.height == b.height &&
           a.format == b.format &&
           std::memcmp(a.buffer->data(), b.buffer->data(), a.byteSize()) == 0;
}

static void testExactAndCoalesced()
{
    printf("\n[JPEG block tiles: exact + one decode per block]\n");
    QTemporaryDir dir;
    QImage img(2300, 1500, QImage::Format_RGB32);
    for (int y = 0; y < img.height(); ++y)
        for (int x = 0; x < img.width(); ++x)
            img.setPixel(x, y, qRgb((x * 7) & 255, (y * 3) & 255, ((x ^ y) * 5) & 255));
    const QString file = QDir(dir.path()).filePath(QStringLiteral("grid.jpg"));
    CHECK(img.save(file, "JPG", 92), "test jpeg written");
    const std::string path = file.toUtf8().toStdString();

    mviewer::core::clearJpegBlockCache();
    int mismatches = 0;
    int tiles = 0;
    // Bottom-right block is clamped (2300x1500 is not a multiple of 1024).
    for (int ty = 0; ty < 1500; ty += 256)
    {
        for (int tx = 0; tx < 2300; tx += 256)
        {
            const int w = std::min(256, 2300 - tx);
            const int h = std::min(256, 1500 - ty);
            CHECK(mviewer::core::jpegBlockEligible(path, tx, ty, w, h, w, h), "tile eligible");
            const ImageData viaBlock =
                mviewer::core::decodeTilePreferReduced(path, ImageData{}, tx, ty, w, h, w, h);
            if (!samePixels(viaBlock, directTile(path, tx, ty, w, h)))
                ++mismatches;
            ++tiles;
        }
    }
    CHECK(mismatches == 0, "every block-sliced tile equals the per-tile decode");
    // 2300x1500 spans 3x2 blocks of 1024.
    CHECK(mviewer::core::jpegBlockDecodeCount() == 6, "one decode per 1024 block");
    printf("  %d tiles, %d block decodes\n", tiles, mviewer::core::jpegBlockDecodeCount());

    CHECK(!mviewer::core::jpegBlockEligible(path, 0, 0, 512, 512, 256, 256),
          "scaled tiles keep the regular path");
    CHECK(!mviewer::core::jpegBlockEligible(path, 900, 0, 256, 256, 256, 256),
          "tile straddling two blocks keeps the regular path");
    CHECK(!mviewer::core::jpegBlockEligible("x.png", 0, 0, 256, 256, 256, 256),
          "non-JPEG keeps the regular path");
}

static void benchLargeJpeg(const char *file)
{
    printf("\n[bench %s]\n", file);
    const std::string path(file);
    auto source = SourceImage::open(path);
    if (!source || !source->isValid())
        return;
    const int w = source->metadata().width;
    const int h = source->metadata().height;
    const int x0 = ((w - 1024) / 1024) * 1024;
    const int y0 = ((h - 768) / 1024) * 1024;
    QElapsedTimer t;
    t.start();
    for (int ty = 0; ty < 3; ++ty)
        for (int tx = 0; tx < 4; ++tx)
            directTile(path, x0 + tx * 256, y0 + ty * 256, 256, 256);
    const qint64 before = t.elapsed();
    mviewer::core::clearJpegBlockCache();
    t.restart();
    for (int ty = 0; ty < 3; ++ty)
        for (int tx = 0; tx < 4; ++tx)
            mviewer::core::decodeTilePreferReduced(path, ImageData{}, x0 + tx * 256, y0 + ty * 256,
                                                   256, 256, 256, 256);
    printf("  12 bottom tiles: per-tile %lld ms, block %lld ms\n", before, t.elapsed());
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    testExactAndCoalesced();
    if (const char *bench = std::getenv("MVIEWER_BENCH_JPEG"))
        benchLargeJpeg(bench);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
// NOLINTEND
