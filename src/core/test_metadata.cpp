// M6 unit tests: ImageMetadata enrichment (bitDepth/channels/colorSpace/
// orientation/format) populated during decode, and registry dispatch of format.
#include "core/image/Decoder.h"
#include "core/image/ImageRepository.h"
#include "core/image/MetadataReader.h"
#include "core/image/decoder/DecoderRegistry.h"

#include <QByteArray>
#include <QColor>
#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

#ifndef MVIEWER_SOURCE_DIR
static std::string srcRootFromThisFile()
{
    std::string f = __FILE__;
    auto p = f.find("/src/core/test_metadata.cpp");
    if (p == std::string::npos)
        p = f.find("\\src\\core\\test_metadata.cpp");
    return p == std::string::npos ? "." : f.substr(0, p);
}
#define MVIEWER_SOURCE_DIR srcRootFromThisFile().c_str()
#endif

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
    } while (0)

// M6 acceptance: the 4 golden images must decode with format/channels/bitDepth
// populated on the resulting ImageFrame metadata. File-level fields (path, size,
// dimensions) must also still be present.
static void testMetadataGolden()
{
    printf("\n[Metadata enrichment — golden images (M6)]\n");
    fflush(stdout);

    ImageRepository &repo = ImageRepository::instance();
    const std::string base = std::string(MVIEWER_SOURCE_DIR) + "/testdata/golden/256x256/";
    struct Golden
    {
        const char *file;
        const char *expectFormat;
    } files[] = {{"flat_color_256x256.jpg", "JPEG"},
                 {"flat_color_256x256.png", "PNG"},
                 {"flat_color_256x256.bmp", "BMP"},
                 {"flat_color_256x256.tiff", "TIFF"}};

    for (const auto &g : files)
    {
        const std::string path = base + g.file;
        auto r = repo.load(path);
        CHECK(r.success(), ("load " + std::string(g.file)).c_str());
        if (!r.success())
            continue;
        const auto &m = r.frame->metadata();
        CHECK(m.format == g.expectFormat,
              (std::string(g.file) + " format == " + g.expectFormat + " (got '" + m.format + "')")
                  .c_str());
        CHECK(m.width == 256 && m.height == 256, "dimensions preserved (256x256)");
        CHECK(m.channels >= 1, "channels populated (>0)");
        CHECK(m.bitDepth > 0, "bitDepth populated (>0)");
        CHECK(m.orientation >= 1 && m.orientation <= 8, "orientation in EXIF range 1-8");
        CHECK(!m.filePath.empty(), "filePath preserved");
        CHECK(m.fileSize > 0, "fileSize preserved");
    }
}

// M6 acceptance: Decoder::decodeFull(path, meta) populates the decode-time
// metadata fields when decoding directly (no repository/cache involved).
static void testDecoderEnrichment()
{
    printf("\n[Decoder metadata enrichment (M6)]\n");
    fflush(stdout);

    const std::string base = std::string(MVIEWER_SOURCE_DIR) + "/testdata/golden/256x256/";
    const std::string path = base + "flat_color_256x256.png";

    mviewer::domain::ImageMetadata meta;
    ImageData img = Decoder::decodeFull(path, meta);
    CHECK(!img.isNull(), "decodeFull(png) returns pixels");
    CHECK(meta.format == "PNG", "Decoder fills format = PNG");
    CHECK(meta.width == 256 && meta.height == 256, "Decoder fills dimensions");
    CHECK(meta.channels >= 1, "Decoder fills channels");
    CHECK(meta.bitDepth > 0, "Decoder fills bitDepth");
}

// M7: MetadataReader is the Manager that ImageRepository delegates metadata/key
// computation to (Review P0-1). Verify it directly.
static void testMetadataReader()
{
    printf("\n[MetadataReader (M7)]\n");
    fflush(stdout);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    const std::string p = (dir.path() + "/mr.png").toStdString();
    {
        QImage img(256, 128, QImage::Format_RGB32);
        img.fill(QColor(10, 20, 30));
        CHECK(img.save(QString::fromStdString(p), "PNG"), "write test png");
    }

    mviewer::domain::ImageMetadata m = mviewer::core::MetadataReader::read(p);
    CHECK(m.width == 256 && m.height == 128, "MetadataReader reads dimensions");
    CHECK(m.fileSize > 0, "MetadataReader reads fileSize");
    CHECK(!m.hash.empty(), "MetadataReader builds hash");
    CHECK(m.fileName == "mr.png", "MetadataReader reads fileName");

    const std::string k1 = mviewer::core::MetadataReader::key(p);
    CHECK(!k1.empty(), "key non-empty");
    // Same path+size+mtime -> stable key.
    CHECK(mviewer::core::MetadataReader::key(p) == k1, "key stable across calls");

    // Missing file -> empty metadata, no throw.
    mviewer::domain::ImageMetadata missing =
        mviewer::core::MetadataReader::read("/no/such/file.png");
    CHECK(missing.fileSize == 0, "missing file -> empty metadata");
}

// Hostile/degenerate EXIF offsets. A crafted file must degrade to "no GPS",
// never to a wild pointer read: the IFD bounds checks used 32-bit addition,
// which wraps for file-supplied offsets such as 0xFFFFFFFF.
static void writeBytes(const std::string &path, const char *hexBytes)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::WriteOnly))
        return;
    f.write(QByteArray::fromHex(hexBytes));
    f.close();
}

static void testMetadataHostileOffsets()
{
    printf("\n[MetadataReader hostile EXIF offsets]\n");
    fflush(stdout);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "hostile-offset temp dir created");

    // (a) II*\0 + IFD0 offset 0xFFFFFFFF: `ifd0 + 2` wraps to 1 and used to pass
    //     the bounds check, dereferencing `data + 0xFFFFFFFF`.
    {
        const std::string p = (dir.path() + "/evil-ifd0.tif").toStdString();
        writeBytes(p, "49492a00ffffffff"); // II*\0 + IFD0 offset 0xFFFFFFFF
        const mviewer::domain::ImageMetadata m = mviewer::core::MetadataReader::read(p);
        CHECK(!m.hasGps, "IFD0 offset 0xFFFFFFFF -> no GPS, no crash");
    }

    // (b) Valid IFD0 whose GPS tag points at 0xFFFFFFFF: the same wrap in the
    //     gpsIfd guard, reaching parseGpsIfd with a wild offset.
    {
        const std::string p = (dir.path() + "/evil-gps.tif").toStdString();
        // II, IFD0 at 8, one entry: tag 0x8825 (GPS IFD), type LONG, count 1,
        // value 0xFFFFFFFF.
        writeBytes(p, "49492a000800000001002588040001000000ffffffff");
        const mviewer::domain::ImageMetadata m = mviewer::core::MetadataReader::read(p);
        CHECK(!m.hasGps, "GPS IFD offset 0xFFFFFFFF -> no GPS, no crash");
    }

    // (c) Entry count far past the buffer: the entry loop must stop at the end.
    {
        const std::string p = (dir.path() + "/evil-count.tif").toStdString();
        // II*\0 + IFD0 at 8 + entry count 65535 with no entries present.
        writeBytes(p, "49492a0008000000ffff");
        const mviewer::domain::ImageMetadata m = mviewer::core::MetadataReader::read(p);
        CHECK(!m.hasGps, "IFD entry count past EOF -> no GPS, no crash");
    }

    // (d) A normal file still reports as before (the fix must not over-reject).
    {
        const std::string p = (dir.path() + "/plain.png").toStdString();
        QImage img(32, 16, QImage::Format_RGB32);
        img.fill(QColor(7, 8, 9));
        CHECK(img.save(QString::fromStdString(p), "PNG"), "write plain png");
        const mviewer::domain::ImageMetadata m = mviewer::core::MetadataReader::read(p);
        CHECK(m.width == 32 && m.height == 16, "well-formed file still reads dimensions");
    }

    // (e) Positive control: a well-formed TIFF with a GPS IFD must still be
    //     parsed. Without this, (a)-(c) could pass by never reaching the GPS
    //     path at all and would not protect the bounds checks they target.
    //     Layout: header(8) IFD0@8(1 entry -> GPS IFD@26) GPS IFD@26(2 entries:
    //     GPSLatitude -> rationals@56, GPSLongitude -> rationals@80).
    {
        const std::string p = (dir.path() + "/valid-gps.tif").toStdString();
        writeBytes(p,
                   "49492a0008000000"                                 // II*\0, IFD0 at 8
                   "0100"                                             // 1 entry
                   "25880400010000001a000000"                         // GPS IFD pointer -> 26
                   "00000000"                                         // no next IFD
                   "0200"                                             // GPS IFD: 2 entries
                   "020005000300000038000000"                         // GPSLatitude -> 56
                   "040005000300000050000000"                         // GPSLongitude -> 80
                   "00000000"                                         // no next IFD
                   "28000000010000001e000000010000000000000001000000" // 40/1, 30/1, 0/1
                   "500000000100000000000000010000000000000001000000" // 80/1, 0/1, 0/1
        );
        const mviewer::domain::ImageMetadata m = mviewer::core::MetadataReader::read(p);
        CHECK(m.hasGps, "well-formed GPS TIFF still reports GPS");
        CHECK(std::abs(m.gpsLatitude - 40.5) < 1e-6 && std::abs(m.gpsLongitude - 80.0) < 1e-6,
              "GPS coordinates parsed exactly");
    }
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Metadata Tests (M6) ===\n");
    fflush(stdout);

    testMetadataGolden();
    testDecoderEnrichment();
    testMetadataReader();
    testMetadataHostileOffsets();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
