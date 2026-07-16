// M6 unit tests: ImageMetadata enrichment (bitDepth/channels/colorSpace/
// orientation/format) populated during decode, and registry dispatch of format.
#include "core/image/ImageRepository.h"
#include "core/image/Decoder.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/MetadataReader.h"

#include <QColor>
#include <QCoreApplication>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
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

#define CHECK(cond, msg)                 \
    do                                   \
    {                                    \
        if (cond)                        \
        {                                \
            printf("  PASS: %s\n", msg); \
            g_pass++;                    \
        }                                \
        else                             \
        {                                \
            printf("  FAIL: %s\n", msg); \
            g_fail++;                    \
        }                                \
    } while (0)

// M6 acceptance: the 4 golden images must decode with format/channels/bitDepth
// populated on the resulting ImageFrame metadata. File-level fields (path, size,
// dimensions) must also still be present.
static void testMetadataGolden()
{
    printf("\n[Metadata enrichment — golden images (M6)]\n");
    fflush(stdout);

    ImageRepository& repo = ImageRepository::instance();
    const std::string base = std::string(MVIEWER_SOURCE_DIR) + "/testdata/golden/256x256/";
    struct Golden
    {
        const char* file;
        const char* expectFormat;
    } files[] = {{"flat_color_256x256.jpg", "JPEG"},
                 {"flat_color_256x256.png", "PNG"},
                 {"flat_color_256x256.bmp", "BMP"},
                 {"flat_color_256x256.tiff", "TIFF"}};

    for (const auto& g : files)
    {
        const std::string path = base + g.file;
        auto r = repo.load(path);
        CHECK(r.success(), ("load " + std::string(g.file)).c_str());
        if (!r.success())
            continue;
        const auto& m = r.frame->metadata();
        CHECK(m.format == g.expectFormat,
              (std::string(g.file) + " format == " + g.expectFormat +
               " (got '" + m.format + "')")
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
    mviewer::domain::ImageMetadata missing = mviewer::core::MetadataReader::read("/no/such/file.png");
    CHECK(missing.fileSize == 0, "missing file -> empty metadata");
}

int main(int argc, char** argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Metadata Tests (M6) ===\n");
    fflush(stdout);

    testMetadataGolden();
    testDecoderEnrichment();
    testMetadataReader();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
