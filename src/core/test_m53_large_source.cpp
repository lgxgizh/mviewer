// M53 Phase 0/1/2 regressions: large-source parity and bounded RAW preview
// scanning. These remain separate from the historical M47 workflow suites so
// format-parity evidence is easy to audit.

#include "core/image/SourceImage.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/RawDecoder.h"

#include <QBuffer>
#include <QFile>
#include <QGuiApplication>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>

#include <algorithm>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <string>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            std::printf("  PASS: %s\n", msg);                                                     \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::printf("  FAIL: %s\n", msg);                                                     \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)

namespace
{

std::string fixture(const char *name)
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large/" + name;
}

QByteArray makeJpegBytes(int w, int h)
{
    QImage image(w, h, QImage::Format_RGB888);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            image.setPixel(x, y, qRgb((x * 255) / std::max(1, w - 1),
                                      (y * 255) / std::max(1, h - 1), (x + y) & 0xff));
    QByteArray encoded;
    QBuffer buffer(&encoded);
    buffer.open(QIODevice::WriteOnly);
    QImageWriter writer(&buffer, "JPEG");
    writer.write(image);
    return encoded;
}

std::vector<uint8_t> makeLargeSyntheticRaw(const QByteArray &smallPreview,
                                            const QByteArray &largePreview,
                                            size_t fillerBytes)
{
    std::vector<uint8_t> raw;
    raw.reserve(4096 + static_cast<size_t>(smallPreview.size()) + fillerBytes +
                static_cast<size_t>(largePreview.size()));
    raw.insert(raw.end(), 2048, 0x5a);
    raw.insert(raw.end(), reinterpret_cast<const uint8_t *>(smallPreview.constData()),
               reinterpret_cast<const uint8_t *>(smallPreview.constData()) + smallPreview.size());
    raw.insert(raw.end(), fillerBytes, 0x5a);
    raw.insert(raw.end(), reinterpret_cast<const uint8_t *>(largePreview.constData()),
               reinterpret_cast<const uint8_t *>(largePreview.constData()) + largePreview.size());
    return raw;
}

void writeBytes(const QString &path, const std::vector<uint8_t> &bytes)
{
    QFile file(path);
    file.open(QIODevice::WriteOnly);
    file.write(reinterpret_cast<const char *>(bytes.data()),
               static_cast<qint64>(bytes.size()));
    file.close();
}

} // namespace

int main(int argc, char **argv)
{
    QGuiApplication app(argc, argv);
    DecoderRegistry::instance().resetToDefaults();

    // ── T1: 100MP TIFF must have a bounded display representation ──────────
    {
        mviewer::core::SourceDecodeStats::instance().counters().reset();
        auto source = mviewer::core::SourceImage::open(fixture("large_tiff_100mp.tiff"));
        CHECK(source != nullptr, "T1: 100MP TIFF metadata probe succeeds");
        if (source)
        {
            auto lod = source->decodeLod(256);
            CHECK(lod.ok && !lod.pixels.isNull(),
                  "T1: 100MP TIFF produces a non-empty bounded fit raster");
            CHECK(lod.pixels.width == 256 && lod.pixels.height == 256,
                  "T1: TIFF fit raster preserves source aspect");
            CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod,
                  "T1: TIFF fit raster is classified by its measured native backend path");
            const auto &c = mviewer::core::SourceDecodeStats::instance().counters();
            CHECK(c.fullDecode.load() == 0 && c.fullDecodeScaled.load() == 0,
                  "T1: TIFF display does not attempt full materialization");
        }
    }

    // ── T2: a TIFF deep-zoom region stays bounded and non-empty ─────────────
    {
        mviewer::core::SourceDecodeStats::instance().counters().reset();
        auto source = mviewer::core::SourceImage::open(fixture("large_tiff_100mp.tiff"));
        CHECK(source != nullptr, "T2: TIFF source reopens for region display");
        if (source)
        {
            auto region = source->decodeRegion({4200, 3100, 640, 480}, 640, 480);
            CHECK(region.ok && !region.pixels.isNull(),
                  "T2: TIFF bounded source region is non-empty");
            CHECK(region.pixels.width == 640 && region.pixels.height == 480,
                  "T2: TIFF region target dimensions are honored");
            CHECK(region.decodePath == mviewer::core::SourceDecodePath::NativeRegion ||
                      region.decodePath == mviewer::core::SourceDecodePath::BoundedRasterRegion,
                  "T2: TIFF region uses an explicitly bounded/native classification");
            const auto &c = mviewer::core::SourceDecodeStats::instance().counters();
            CHECK(c.fullDecode.load() == 0 && c.fullDecodeCrop.load() == 0,
                  "T2: TIFF region does not materialize the full source");
        }
    }

    // ── T3: remaining raster formats keep an honest bounded-display contract
    {
        for (const char *name : {"large_tiff_16bit.tiff", "large_png_16mp.png",
                                 "large_bmp_16mp.bmp"})
        {
            mviewer::core::SourceDecodeStats::instance().counters().reset();
            auto source = mviewer::core::SourceImage::open(fixture(name));
            CHECK(source != nullptr, "T3: large raster metadata probe succeeds");
            if (!source)
                continue;
            const auto lod = source->decodeLod(256);
            CHECK(lod.ok && !lod.pixels.isNull(), "T3: large raster produces a display LOD");
            CHECK(lod.pixels.width > 0 && lod.pixels.height > 0,
                  "T3: large raster LOD has valid dimensions");
            if (std::string(name).find("tiff") != std::string::npos)
                CHECK(lod.decodePath == mviewer::core::SourceDecodePath::NativeLod,
                      "T3: 16-bit TIFF uses the native WIC bounded path");
            else
                CHECK(lod.decodePath == mviewer::core::SourceDecodePath::FullDecodeScaled,
                      "T3: PNG/BMP remain honestly classified as scaled fallback");
        }
    }

    // ── T4: Unicode + spaces remain source-backed path-safe ─────────────────
    {
        QTemporaryDir dir;
        const QString unicodePath = dir.path() + "/大图 space 🚀.tiff";
        const QString sourcePath = QString::fromStdString(fixture("large_tiff_16bit.tiff"));
        CHECK(QFile::copy(sourcePath, unicodePath),
              "T4: Unicode large-source fixture copied");
        auto source = mviewer::core::SourceImage::open(unicodePath.toUtf8().toStdString());
        CHECK(source != nullptr, "T4: Unicode TIFF metadata probe succeeds");
        if (source)
        {
            const auto lod = source->decodeLod(128);
            CHECK(lod.ok && !lod.pixels.isNull(),
                  "T4: Unicode TIFF produces a bounded display raster");
        }
    }

    // ── T5: the scanner's storage must not follow a 64MiB container ─────────
    {
        QTemporaryDir dir;
        CHECK(dir.isValid(), "T5: temporary RAW directory created");
        const QByteArray small = makeJpegBytes(32, 24);
        const QByteArray large = makeJpegBytes(320, 240);
        const auto raw = makeLargeSyntheticRaw(small, large, 64u * 1024u * 1024u);
        const QString path = dir.path() + "/large-preview.cr2";
        writeBytes(path, raw);
        RawDecoder decoder;
        const ImageData image = decoder.decodeFull(path.toStdString());
        CHECK(!image.isNull() && image.width == 320 && image.height == 240,
              "T5: large synthetic RAW selects the largest valid embedded JPEG");
        CHECK(RawDecoder::lastPreviewPeakBufferedBytes() < 8u * 1024u * 1024u,
              "T5: RAW scanner peak storage is bounded independently of container size");
        CHECK(RawDecoder::lastPreviewFullFileCopyBytes() == 0,
              "T5: RAW scanner has no second full-file copy");
    }

    // ── T6: malformed large RAW remains a graceful terminal failure ─────────
    {
        QTemporaryDir dir;
        const QString path = dir.path() + "/truncated.cr2";
        QFile file(path);
        file.open(QIODevice::WriteOnly);
        QByteArray prefix(32u * 1024u * 1024u, '\x5a');
        file.write(prefix);
        file.write(QByteArray::fromHex("ffd8ffe000104a464946000101"));
        file.close();
        RawDecoder decoder;
        CHECK(decoder.decodeFull(path.toStdString()).isNull(),
              "T6: truncated embedded preview fails gracefully");
        CHECK(RawDecoder::lastPreviewPeakBufferedBytes() < 8u * 1024u * 1024u,
              "T6: malformed RAW scan remains bounded");
    }

    std::printf("\n==== M53 large-source regressions: %d passed, %d failed ====\n", g_pass, g_fail);
    std::fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
