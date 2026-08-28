// M47 Phase 1 — source-backed image abstraction tests.
//
// Proves the capability contract with the deterministic large-image corpus:
//   T1  probe: metadata WITHOUT pixel decode (100 MP JPEG + TIFF)
//   T2  native LOD: 100 MP JPEG decodeLod(256) succeeds, classified NativeLod,
//       fullDecode counter stays 0 (a full decode would be rejected by Qt's
//       256 MB allocation limit — success proves no full raster)
//   T3  100 MP TIFF decodeLod uses the native Windows WIC bounded path; prove
//       the source-backed raster is non-empty and never falls back to a full
//       decode
//   T4  bounded region: 100 MP JPEG decodeRegion succeeds at 512x512,
//       classified BoundedRasterRegion, fullDecode counter stays 0
//   T5  region correctness: clip-path region == full-decode crop region
//       (pixel-identical) on a deterministic small image
//   T6  fallback without capabilities: a delegating decoder WITHOUT the
//       interface routes through FullDecodeScaled/FullDecodeCrop and the
//       fallbackNoCapability counter increments
//   T7  corrupt/truncated + missing: probe may answer, decode fails cleanly;
//       missing file -> nullptr
//   T8  EXIF orientation probe: displayed geometry is swapped for orientation 6
//
// Decoder registry mutations (T6) are test-local, restored between cases;
// production code is untouched.

#include "core/image/Decoder.h"
#include "core/image/SourceImage.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <QTemporaryDir>

#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>

static int g_failures = 0;

#define CHECK(c, m)                                                                                 \
    do                                                                                              \
    {                                                                                               \
        if (!(c))                                                                                   \
        {                                                                                           \
            std::printf("FAIL: %s\n", m);                                                           \
            std::fflush(stdout);                                                                    \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

#define MARK(t)                                                                                     \
    do                                                                                              \
    {                                                                                               \
        std::printf("%s\n", t);                                                                     \
        std::fflush(stdout);                                                                        \
    } while (false)

namespace
{

using ::ImageData; // global scope (ImageBuffer.h)
using mviewer::core::SourceDecodePath;
using mviewer::core::SourceImage;
using mviewer::core::SourceDecodeStats;

std::string fixtureRoot()
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large";
}

std::string fixture(const char *name)
{
    return fixtureRoot() + "/" + name;
}

// Delegating decoder WITHOUT the capability interface (fallback-path probe).
class PlainDelegatingDecoder : public IDecoder
{
  public:
    bool canDecode(const std::string &path) const override
    {
        return m_inner.canDecode(path);
    }
    ImageData decodeFull(const std::string &path) const override
    {
        return m_inner.decodeFull(path);
    }
    ImageData decodeScaled(const std::string &path, int maxEdge) const override
    {
        return m_inner.decodeScaled(path, maxEdge);
    }
    ImageData decodeScaled(const std::string &path, int maxEdge,
                           mviewer::domain::ImageMetadata &meta) const override
    {
        return m_inner.decodeScaled(path, maxEdge, meta);
    }
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &meta) const override
    {
        return m_inner.decodeFull(path, meta);
    }
    std::vector<std::string> extensions() const override { return m_inner.extensions(); }
    const char *name() const override { return "PlainDelegatingDecoder"; }

  private:
    QtDecoder m_inner;
};

// Deterministic 300x200 RGB image: vertical gradient + a red marker block.
QImage makeProbeImage()
{
    QImage img(300, 200, QImage::Format_RGB32);
    for (int y = 0; y < 200; ++y)
        for (int x = 0; x < 300; ++x)
            img.setPixel(x, y, qRgb(x * 255 / 300, y * 255 / 200, (x + y) * 255 / 500));
    for (int y = 80; y < 120; ++y)
        for (int x = 140; x < 200; ++x)
            img.setPixel(x, y, qRgb(255, 0, 0));
    return img;
}

QString writeProbeJpeg(const QTemporaryDir &dir)
{
    const QString p = dir.path() + "/probe.jpg";
    makeProbeImage().save(p, "JPEG", 92);
    return p;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    if (!QDir(QString::fromStdString(fixtureRoot())).exists())
    {
        std::printf("fixture root missing: %s\n", fixtureRoot().c_str());
        std::printf("run: python testdata/generate_large_fixtures.py --ensure\n");
        return 2;
    }

    const std::string jpeg100 = fixture("large_jpeg_100mp.jpg");
    const std::string tiff100 = fixture("large_tiff_100mp.tiff");
    const std::string orient6 = fixture("exif_orientation6_wide.jpg");
    const std::string truncated = fixture("truncated_large.jpg");

    // ── T1: probe without pixel decode ───────────────────────────────────────
    {
        MARK("T1 start");
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(jpeg100);
        CHECK(src != nullptr, "T1: 100MP JPEG opens as a source (probe only)");
        if (src)
        {
            CHECK(src->metadata().width == 12000 && src->metadata().height == 8333,
                  "T1: probe reports 12000x8333 without decoding");
            CHECK(src->metadata().format == "JPEG", "T1: probe reports JPEG format");
            CHECK(src->hasCapabilities(), "T1: QtDecoder implements the capability interface");
        }
        auto tiff = SourceImage::open(tiff100);
        CHECK(tiff != nullptr, "T1: 100MP TIFF opens as a source (probe only)");
        if (tiff)
            CHECK(tiff->metadata().width == 10000 && tiff->metadata().height == 10000,
                  "T1: TIFF probe reports 10000x10000");
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.probe.load() >= 2, "T1: probe counter recorded");
        CHECK(c.fullDecode.load() == 0, "T1: NO full decode happened for probes");
    }

    // ── T2: native LOD on the 100 MP JPEG ───────────────────────────────────
    {
        MARK("T2 start");
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(jpeg100);
        CHECK(src && src->hasNativeLod(), "T2: JPEG advertises native LOD");
        if (src)
        {
            auto lodR = src->decodeLod(256);
            ImageData lod = lodR.pixels;
            CHECK(lodR.ok && !lod.isNull(), "T2: 100MP JPEG LOD(256) succeeds");
            CHECK(lod.width == 256 && lod.height == 177,
                  "T2: LOD output is 256x177 (aspect preserved)");
            CHECK(lodR.decodePath == SourceDecodePath::NativeLod,
                  "T2: classified NativeLod (no full raster)");
        }
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.nativeLod.load() == 1, "T2: nativeLod counter == 1");
        CHECK(c.fullDecode.load() == 0,
              "T2: fullDecode counter == 0 (a full decode would be rejected by "
              "Qt's 256 MB limit, so success proves the LOD path)");
    }

    // ── T3: native bounded LOD on the 100 MP TIFF ───────────────────────────
    {
        MARK("T3 start");
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(tiff100);
        CHECK(src && src->hasNativeLod(), "T3: TIFF advertises the native bounded WIC LOD");
        if (src)
        {
            auto lodR = src->decodeLod(256);
            ImageData lod = lodR.pixels;
            CHECK(lodR.ok && !lod.isNull(), "T3: 100MP TIFF LOD(256) returns a bounded raster");
            CHECK(lod.width == 256 && lod.height == 256,
                  "T3: TIFF LOD output preserves the square aspect");
            CHECK(lodR.decodePath == SourceDecodePath::NativeLod,
                  "T3: classified NativeLod through WIC");
        }
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.nativeLod.load() == 1, "T3: nativeLod counter == 1");
        CHECK(c.fullDecode.load() == 0 && c.fullDecodeScaled.load() == 0,
              "T3: no full-source fallback was attempted");
        CHECK(c.failed.load() == 0, "T3: bounded TIFF decode has no failure");
    }

    // ── T4: bounded region decode on the 100 MP JPEG ────────────────────────
    {
        MARK("T4 start");
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(jpeg100);
        CHECK(src && !src->hasNativeRegion(), "T4: no native-region claim (honest)");
        if (src)
        {
            auto regionR = src->decodeRegion({1000, 1000, 512, 512}, 512, 512);
            ImageData region = regionR.pixels;
            CHECK(regionR.ok && !region.isNull(),
                  "T4: 100MP JPEG region(512x512) succeeds (bounded memory)");
            CHECK(region.width == 512 && region.height == 512,
                  "T4: region output is 512x512");
            CHECK(regionR.decodePath == SourceDecodePath::BoundedRasterRegion,
                  "T4: classified BoundedRasterRegion (clip path, not native)");
        }
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.boundedRegion.load() == 1, "T4: boundedRegion counter == 1");
        CHECK(c.fullDecode.load() == 0,
              "T4: no full raster materialized (would have been rejected by the limit)");
    }

    // ── T5: region correctness (clip path == full-decode crop) ──────────────
    {
        MARK("T5 start");
        QTemporaryDir dir;
        const QString p = writeProbeJpeg(dir);
        const std::string path = p.toStdString();
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(path);
        CHECK(src != nullptr, "T5: probe image opens");
        if (src)
        {
            // Bounded clip path.
            auto regionR = src->decodeRegion({50, 50, 100, 80}, 100, 80);
            ImageData region = regionR.pixels;
            CHECK(regionR.ok && !region.isNull(), "T5: region decode succeeds");
            // Reference: full decode + cropRegion (exact source semantics).
            mviewer::domain::ImageMetadata meta;
            ImageData full = Decoder::decodeFull(path, meta);
            CHECK(!full.isNull(), "T5: full reference decode succeeds");
            if (!region.isNull() && !full.isNull())
            {
                const mviewer::domain::Selection sel{50, 50, 100, 80};
                ImageData ref = cropRegion(full, sel);
                CHECK(ref.width == region.width && ref.height == region.height,
                      "T5: region dims match the reference crop");
                bool identical = true;
                if (ref.width == region.width && ref.height == region.height)
                {
                    for (int y = 0; y < region.height && identical; ++y)
                    {
                        const uint8_t *a = region.buffer->data() +
                                           static_cast<size_t>(y) * region.stride();
                        const uint8_t *b = ref.buffer->data() +
                                           static_cast<size_t>(y) * ref.stride();
                        for (int x = 0; x < region.width; ++x)
                            for (int ch = 0; ch < 3; ++ch)
                                if (a[x * 3 + ch] != b[x * 3 + ch])
                                {
                                    identical = false;
                                    break;
                                }
                    }
                }
                CHECK(identical,
                      "T5: clip-path region is pixel-identical to the full-decode crop");
            }
        }
    }

    // ── T6: fallback path without the capability interface ──────────────────
    {
        MARK("T6 start");
        // Registry mutation is test-local: make the non-capability decoder the
        // first claimer for jpeg files, then restore defaults afterwards.
        auto &registry = DecoderRegistry::instance();
        registry.resetToDefaults();
        registry.unregister("QtDecoder");
        registry.unregister("QtFallbackDecoder");
        registry.registerDecoder(std::make_shared<PlainDelegatingDecoder>());
        registry.registerDecoder(std::make_shared<QtDecoder>());
        registry.registerDecoder(std::make_shared<QtFallbackDecoder>());

        QTemporaryDir dir;
        const QString p = writeProbeJpeg(dir);
        const std::string path = p.toStdString();
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(path);
        CHECK(src != nullptr, "T6: fallback source opens");
        CHECK(src && !src->hasCapabilities(), "T6: no capability interface on the claimer");
        if (src)
        {
            auto lodR = src->decodeLod(128);
            ImageData lod = lodR.pixels;
            CHECK(lodR.ok && !lod.isNull(), "T6: fallback LOD succeeds via decodeScaled");
            CHECK(lodR.decodePath == SourceDecodePath::FullDecodeScaled,
                  "T6: fallback LOD classified FullDecodeScaled");
            auto regionR = src->decodeRegion({10, 10, 64, 64}, 64, 64);
            CHECK(regionR.ok && !regionR.pixels.isNull(),
                  "T6: fallback region succeeds via decodeFull+crop");
            CHECK(regionR.pixels.width == 64 && regionR.pixels.height == 64,
                  "T6: fallback region dims");
            CHECK(regionR.decodePath == SourceDecodePath::FullDecodeCrop,
                  "T6: fallback region classified FullDecodeCrop");
        }
        const auto &c = SourceDecodeStats::instance().counters();
        CHECK(c.fallbackNoCapability.load() >= 2, "T6: fallbackNoCapability counter >= 2");
        CHECK(c.fullDecode.load() >= 1, "T6: the fallback region really called decodeFull");

        registry.resetToDefaults(); // restore the frozen lineup for later cases
    }

    // ── T7: corrupt/truncated and missing input ─────────────────────────────
    {
        MARK("T7 start");
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(truncated);
        // The truncation keeps the header intact, so the probe can still
        // answer. The decode must be GRACEFUL: Qt's JPEG plugin either returns
        // a partial (top-region) image or fails — never a crash, never a
        // buffer overrun. Both outcomes are acceptable; the failure must be
        // recorded when it occurs.
        if (src)
        {
            auto lodR = src->decodeLod(256);
            ImageData lod = lodR.pixels;
            CHECK(!lod.isNull() || SourceDecodeStats::instance().counters().failed.load() >= 1,
                  "T7: truncated image LOD is graceful (partial image OR recorded failure)");
        }
        else
        {
            CHECK(true, "T7: truncated image probe may fail; open() returned null safely");
        }
        auto missing = SourceImage::open(fixtureRoot() + "/does_not_exist.jpg");
        CHECK(missing == nullptr, "T7: missing file -> nullptr (no crash)");
    }

    // ── T8: EXIF orientation probe reports displayed geometry ──────────────
    {
        MARK("T8 start");
        SourceDecodeStats::instance().counters().reset();
        auto src = SourceImage::open(orient6);
        CHECK(src != nullptr, "T8: orientation-6 fixture opens");
        if (src)
        {
            // Raw file is 4000x2000; orientation 6 rotates 90 CW, so the
            // displayed geometry is 2000x4000 (same as decodeFull semantics).
            CHECK(src->metadata().width == 2000 && src->metadata().height == 4000,
                  "T8: probe reports the DISPLAYED 2000x4000 geometry for orientation 6");
            CHECK(src->metadata().orientation == 6, "T8: orientation field == 6");
            auto lodR = src->decodeLod(128);
            ImageData lod = lodR.pixels;
            CHECK(lodR.ok && !lod.isNull(), "T8: oriented LOD decodes");
            if (!lod.isNull())
            {
                // Displayed geometry is 2000x4000; a max-edge-128 LOD of the
                // rotated image is 64x128 (aspect 1:2 preserved).
                CHECK(lod.width == 64 && lod.height == 128,
                      "T8: oriented LOD respects the rotated aspect (64x128)");
            }
        }
    }

    std::printf("=== M47 source-backed abstraction tests: %s ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
