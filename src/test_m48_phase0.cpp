// M48 Phase 0 — deterministic RED regressions (frozen BEFORE implementation).
//
// Every case asserts the M48 contract that the current M47 code violates; the
// suite must FAIL until the corresponding Phase 1/2 fix lands, then stay green
// forever. Contract summary:
//   A0  normal full-frame display applies the embedded ICC (reference; green).
//   A1  Viewer source-backed LOD display applies the embedded ICC (RED: the
//       raster path calls mvcore::toQImage() directly, skipping display ICC).
//   A2  Compare source-backed pane display applies the embedded ICC (RED: the
//       pane materializer copies convMeta BEFORE decodeLod, and the probe
//       metadata carries no ICC key at all).
//   B1/B2  Viewer visible-region rasters use ORIENTED coordinates, but
//       decodeRegion consumes RAW source coordinates (RED: the decoded region
//       shows the wrong corner content for EXIF 6/2 non-square sources).
//   B3  probe geometry matches the displayed EXIF geometry for orientations
//       2..8 (contract; may be green).
//   C   resize/fullscreen re-request a raster at the new viewport density
//       (contract; may be green).
//   D   Compare deep zoom reaches viewport/source density instead of a
//       fixed whole-image LOD (RED: the pane raster density stays far below
//       the viewport density at ~2.3x zoom).
//   E   source-backed placeholder metadata is complete (fileName, fileSize,
//       modifiedEpochSec, orientation, ICC) (RED: the caps probe path fills
//       only filePath/size/dims/orientation/format).
//   F1  a throwing probe must not escape CompareWorkspace::setImages on the
//       UI thread (RED: SourceImage::open throws inside queueLoadRequests).
//   F2  a throwing decodeLod reaches an observable terminal (loadFailed) and
//       pools drain (RED: the contained worker failure never marshals).
//   F3  a throwing decodeRegion keeps the current raster and reaches the same
//       terminal contract (RED for the terminal signal; raster-keep is green).

#include "compareworkspace.h"
#include "core/compare/CompareEngine.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"
#include "core/image/SourceImage.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"
#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QColorSpace>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImageReader>
#include <QSize>
#include <QTimer>
#include <QWheelEvent>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

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

using mviewer::core::SourceDecodeStats;

std::string fixtureRoot()
{
    return std::string(MVIEWER_SOURCE_DIR) + "/testdata/large";
}

std::string fixture(const char *name)
{
    return fixtureRoot() + "/" + name;
}

QString qfix(const char *name)
{
    return QString::fromStdString(fixture(name));
}

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

bool schedulerIdle()
{
    uint64_t pending = 0;
    uint64_t active = 0;
    for (int p = 0; p < 5; ++p)
    {
        const auto m = TaskScheduler::instance().metrics(static_cast<TaskScheduler::PoolType>(p));
        pending += m.pending;
        active += m.active_tasks;
    }
    return pending == 0 && active == 0;
}

RawImageView *paneView(CompareWorkspace *ws, int index)
{
    for (RawImageView *v : ws->findChildren<RawImageView *>())
    {
        if (v && v->cellIndex() == index)
            return v;
    }
    return nullptr;
}

bool comparePanesReady(CompareWorkspace *ws, int expected)
{
    if (ws->comparedImageCount() != expected)
        return false;
    for (int i = 0; i < expected; ++i)
    {
        RawImageView *v = paneView(ws, i);
        if (!v || v->image().isNull())
            return false;
    }
    return true;
}

// Independent display conversion reference: read the embedded ICC profile from
// the file via a tiny scaled decode (the profile rides the JPEG APP2 marker)
// and convert `rawRgb` through QColorSpace directly — never through
// toDisplayQImage (the code path under test).
QRgb expectedDisplayRgb(const QString &path, QRgb rawRgb)
{
    QColorSpace cs;
    {
        QImageReader reader(path);
        reader.setScaledSize(QSize(16, 16));
        QImage tiny;
        if (reader.read(&tiny) && tiny.colorSpace().isValid())
            cs = tiny.colorSpace();
    }
    QImage probe(1, 1, QImage::Format_RGB32);
    probe.fill(rawRgb);
    if (cs.isValid())
    {
        probe.setColorSpace(cs);
        probe.convertToColorSpace(QColorSpace::SRgb);
    }
    return probe.pixel(0, 0);
}

// 5x5 average at a raster location (kills JPEG/LOD sampling noise).
QRgb rasterSample(const QImage &img, int x, int y)
{
    long long r = 0, g = 0, b = 0;
    int n = 0;
    for (int dy = -2; dy <= 2; ++dy)
    {
        for (int dx = -2; dx <= 2; ++dx)
        {
            const int sx = qBound(0, x + dx, img.width() - 1);
            const int sy = qBound(0, y + dy, img.height() - 1);
            const QRgb p = img.pixel(sx, sy);
            r += qRed(p);
            g += qGreen(p);
            b += qBlue(p);
            ++n;
        }
    }
    return qRgb(static_cast<int>(r / n), static_cast<int>(g / n), static_cast<int>(b / n));
}

bool colorClose(QRgb a, QRgb b, int tol)
{
    return std::abs(qRed(a) - qRed(b)) <= tol && std::abs(qGreen(a) - qGreen(b)) <= tol &&
           std::abs(qBlue(a) - qBlue(b)) <= tol;
}

// ── Throwing decoder (test-local instrumentation, registry-mutating) ─────────
class ThrowingM48Decoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
    enum class Mode
    {
        None,
        Probe,
        Lod,
        Region
    };
    std::atomic<Mode> mode{Mode::None};
    mutable std::atomic<int> probeCalls{0};
    mutable std::atomic<int> lodCalls{0};
    mutable std::atomic<int> regionCalls{0};

    bool canDecode(const std::string &path) const override
    {
        return QFileInfo(QString::fromStdString(path)).suffix().toLower() == "m48";
    }
    ImageData decodeFull(const std::string &) const override { throw std::runtime_error("m48"); }
    ImageData decodeFull(const std::string &, mviewer::domain::ImageMetadata &) const override
    {
        throw std::runtime_error("m48");
    }
    ImageData decodeScaled(const std::string &, int) const override
    {
        throw std::runtime_error("m48");
    }
    ImageData decodeScaled(const std::string &, int, mviewer::domain::ImageMetadata &) const override
    {
        throw std::runtime_error("m48");
    }
    std::vector<std::string> extensions() const override { return {"m48"}; }
    const char *name() const override { return "ThrowingM48Decoder"; }

    bool canProbe(const std::string &) const override { return true; }
    bool probeMetadata(const std::string &path, mviewer::domain::ImageMetadata &meta) const override
    {
        ++probeCalls;
        if (mode == Mode::Probe)
            throw std::runtime_error("injected probe failure");
        meta.filePath = path;
        meta.fileName = "throwing.m48";
        meta.width = 5000;
        meta.height = 4000;
        meta.fileSize = 1024;
        meta.modifiedEpochSec = 1700000000;
        meta.orientation = 1;
        meta.format = "M48";
        return true;
    }
    bool canNativeLod(const std::string &) const override { return true; }
    ImageData decodeLod(const std::string &, int, mviewer::domain::ImageMetadata &) const override
    {
        ++lodCalls;
        if (mode == Mode::Lod)
            throw std::runtime_error("injected lod failure");
        return makeSolidRgb(64, 64, 200, 60, 30);
    }
    bool canNativeRegion(const std::string &) const override { return false; }
    ImageData decodeRegion(const std::string &, int, int, int, int, int, int,
                           mviewer::domain::ImageMetadata &) const override
    {
        ++regionCalls;
        if (mode == Mode::Region)
            throw std::runtime_error("injected region failure");
        return makeSolidRgb(64, 64, 30, 60, 200);
    }

  private:
    static ImageData makeSolidRgb(int w, int h, uint8_t r, uint8_t g, uint8_t b)
    {
        ImageData out = makeImageData(w, h, PixelFormat::RGB24);
        const auto v = out.view();
        for (int y = 0; y < h; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < w; ++x)
            {
                p[x * 3] = r;
                p[x * 3 + 1] = g;
                p[x * 3 + 2] = b;
            }
        }
        return out;
    }
};

std::shared_ptr<ThrowingM48Decoder> g_throwing;

void installThrowing(ThrowingM48Decoder::Mode mode)
{
    auto &registry = DecoderRegistry::instance();
    registry.resetToDefaults();
    // QtFallbackDecoder::canDecode() matches everything and must not shadow
    // the .m48 claiming decoder used by the fault-injection cases.
    registry.unregister("QtFallbackDecoder");
    g_throwing = std::make_shared<ThrowingM48Decoder>();
    g_throwing->mode = mode;
    registry.registerDecoder(g_throwing);
    registry.registerDecoder(std::make_shared<QtDecoder>());
    registry.registerDecoder(std::make_shared<QtFallbackDecoder>());
}

void installDefaults()
{
    auto &registry = DecoderRegistry::instance();
    registry.resetToDefaults();
    registry.registerDecoder(std::make_shared<QtDecoder>());
    registry.registerDecoder(std::make_shared<QtFallbackDecoder>());
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

    const QString adobe100 = qfix("icc_adobe_100mp.jpg");
    const QString adobe12 = qfix("icc_adobe_12mp.jpg");
    const QString jpeg100 = qfix("large_jpeg_100mp.jpg");
    const QString hf72 = qfix("deepzoom_hf_72mp.jpg");
    const QString throwingPath = QString::fromStdString(fixtureRoot()) + "/throwing.m48";
    // The raw patch color embedded by the generator (yellowish-green).
    const QRgb patchRaw = qRgb(128, 224, 0);
    // Patch centers in RAW source coordinates (no rotation on ICC fixtures).
    const QPoint patch100(6100, 4280); // 12000x8333, patch (6020,4200,160,160)
    const QPoint patch12(2000, 1500);  // 4000x3000, patch (1952,1452,96,96)

    // ── A0: normal full-frame display applies the embedded ICC (reference) ───
    {
        MARK("A0 start");
        auto res = ImageRepository::instance().load(adobe12.toStdString());
        CHECK(res.frame && !res.frame->pixels().isNull(), "A0: 12MP frame loads");
        if (res.frame)
        {
            const QImage display =
                mvcore::toDisplayQImage(res.frame->pixels(), res.frame->metadata());
            const QPoint p((patch12.x() * display.width()) / 4000,
                           (patch12.y() * display.height()) / 3000);
            const QRgb sample = rasterSample(display, p.x(), p.y());
            const QRgb expected = expectedDisplayRgb(adobe12, patchRaw);
            printf("  A0: display sample=(%d,%d,%d) expected=(%d,%d,%d)\n", qRed(sample),
                   qGreen(sample), qBlue(sample), qRed(expected), qGreen(expected),
                   qBlue(expected));
            std::fflush(stdout);
            CHECK(!colorClose(patchRaw, expected, 6),
                  "A0: the AdobeRGB patch is genuinely non-sRGB (reference sanity)");
            CHECK(colorClose(sample, expected, 12),
                  "A0: normal full-frame display applies the ICC conversion");
        }
    }

    // ── A1: Viewer source-backed LOD display applies the embedded ICC (RED) ──
    {
        MARK("A1 start");
        installDefaults();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        viewer.setBrowseSequence({adobe100});
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setImage(adobe100);
        CHECK(waitTrue([&] { return ready; }, 60000), "A1: 100MP AdobeRGB opens");
        const QImage raster = viewer.displayRaster();
        CHECK(!raster.isNull(), "A1: LOD raster present");
        if (!raster.isNull())
        {
            const QPoint p((patch100.x() * raster.width()) / 12000,
                           (patch100.y() * raster.height()) / 8333);
            const QRgb sample = rasterSample(raster, p.x(), p.y());
            const QRgb expected = expectedDisplayRgb(adobe100, patchRaw);
            printf("  A1: LOD sample=(%d,%d,%d) expected=(%d,%d,%d) raw=(%d,%d,%d)\n",
                   qRed(sample), qGreen(sample), qBlue(sample), qRed(expected),
                   qGreen(expected), qBlue(expected), qRed(patchRaw), qGreen(patchRaw),
                   qBlue(patchRaw));
            std::fflush(stdout);
            CHECK(colorClose(sample, expected, 14),
                  "A1: viewer LOD display applies the ICC conversion (RED today)");
        }
    }

    // ── A2: Compare source-backed pane display applies the embedded ICC (RED)
    {
        MARK("A2 start");
        installDefaults();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({adobe100, adobe100});
        CHECK(waitTrue([&] { return comparePanesReady(&ws, 2); }, 60000),
              "A2: source-backed panes display");
        RawImageView *v0 = paneView(&ws, 0);
        if (v0)
        {
            const QImage &pane = v0->image();
            const QPoint p((patch100.x() * pane.width()) / 12000,
                           (patch100.y() * pane.height()) / 8333);
            const QRgb sample = rasterSample(pane, p.x(), p.y());
            const QRgb expected = expectedDisplayRgb(adobe100, patchRaw);
            printf("  A2: pane sample=(%d,%d,%d) expected=(%d,%d,%d) raw=(%d,%d,%d)\n",
                   qRed(sample), qGreen(sample), qBlue(sample), qRed(expected),
                   qGreen(expected), qBlue(expected), qRed(patchRaw), qGreen(patchRaw),
                   qBlue(patchRaw));
            std::fflush(stdout);
            CHECK(colorClose(sample, expected, 14),
                  "A2: compare source-backed pane applies the ICC conversion (RED today)");
        }
    }

    // ── B1/B2: viewer visible-region rasters use oriented display coordinates ─
    {
        MARK("B1 start");
        installDefaults();
        SourceDecodeStats::instance().counters().reset();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        viewer.setBrowseSequence({qfix("exif_orient6_non_square.jpg")});
        bool ready = false;
        QSize readySize;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &s)
                         {
                             readySize = s;
                             ready = true;
                         });
        viewer.setImage(qfix("exif_orient6_non_square.jpg"));
        CHECK(waitTrue([&] { return ready; }, 60000),
              "B1: orientation-6 non-square opens");
        CHECK(readySize == QSize(4000, 6000),
              "B1: probe reports the DISPLAYED geometry (4000x6000)");
        // Zoom to 100% and pan so the displayed top-left corner is at the
        // widget origin; the region request must cover it.
        viewer.zoomActual();
        viewer.setViewTransform(Viewport(viewer.width(), viewer.height(), 1.0, 0.0, 0.0));
        viewer.update();
        CHECK(waitTrue(
                  [&]
                  {
                      const auto &c = SourceDecodeStats::instance().counters();
                      return c.boundedRegion.load() >= 1;
                  },
                  60000),
              "B1: a visible-region raster is requested after pan");
        pump(500);
        const QImage raster = viewer.displayRaster();
        // Displayed TL under orientation 6 (90 deg CW) = RAW bottom-left = the
        // BLUE corner marker of the fixture pattern.
        if (!raster.isNull())
        {
            const QRgb tl = rasterSample(raster, 8, 8);
            printf("  B1: region TL sample=(%d,%d,%d)\n", qRed(tl), qGreen(tl), qBlue(tl));
            std::fflush(stdout);
            CHECK(colorClose(tl, qRgb(0, 0, 255), 40),
                  "B1: the region raster shows the oriented TL corner (RED today: "
                  "oriented coords fed to raw decodeRegion)");
        }
        else
        {
            CHECK(false, "B1: region raster missing");
        }
    }

    {
        MARK("B2 start");
        installDefaults();
        SourceDecodeStats::instance().counters().reset();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        viewer.setBrowseSequence({qfix("exif_orient2_non_square.jpg")});
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setImage(qfix("exif_orient2_non_square.jpg"));
        CHECK(waitTrue([&] { return ready; }, 60000), "B2: orientation-2 opens");
        viewer.zoomActual();
        viewer.setViewTransform(Viewport(viewer.width(), viewer.height(), 1.0, 0.0, 0.0));
        viewer.update();
        CHECK(waitTrue(
                  [&]
                  {
                      const auto &c = SourceDecodeStats::instance().counters();
                      return c.boundedRegion.load() >= 1;
                  },
                  60000),
              "B2: visible-region raster requested");
        pump(500);
        const QImage raster = viewer.displayRaster();
        // Displayed TL under orientation 2 (mirror horizontal) = RAW top-right
        // = the GREEN corner marker.
        if (!raster.isNull())
        {
            const QRgb tl = rasterSample(raster, 8, 8);
            printf("  B2: region TL sample=(%d,%d,%d)\n", qRed(tl), qGreen(tl), qBlue(tl));
            std::fflush(stdout);
            CHECK(colorClose(tl, qRgb(0, 255, 0), 40),
                  "B2: mirrored region shows the oriented TL corner (RED today)");
        }
        else
        {
            CHECK(false, "B2: region raster missing");
        }
    }

    // ── B3: probe geometry matches the displayed EXIF geometry (2..8) ───────
    {
        MARK("B3 start");
        installDefaults();
        for (int orient = 2; orient <= 8; ++orient)
        {
            const char *names[] = {"exif_orient2_non_square.jpg", "exif_orient3_non_square.jpg",
                                   "exif_orient4_non_square.jpg", "exif_orient5_non_square.jpg",
                                   "exif_orient6_non_square.jpg", "exif_orient7_non_square.jpg",
                                   "exif_orient8_non_square.jpg"};
            auto src = mviewer::core::SourceImage::open(fixture(names[orient - 2]));
            CHECK(src != nullptr, "B3: probe opens");
            if (!src)
                continue;
            const auto &m = src->metadata();
            printf("  B3: orientation %d probe = %dx%d orientation=%d\n", orient, m.width,
                   m.height, m.orientation);
            std::fflush(stdout);
            CHECK(m.orientation == orient, "B3: orientation field matches the tag");
            const bool swaps = (orient == 5 || orient == 6 || orient == 7 || orient == 8);
            CHECK((swaps ? m.width == 4000 && m.height == 6000 : m.width == 6000 && m.height == 4000),
                  "B3: probe geometry matches the DISPLAYED orientation geometry");
        }
    }

    // ── C: resize / maximize / fullscreen keep a correct fit display ─────────
    {
        MARK("C start");
        installDefaults();
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        viewer.setBrowseSequence({jpeg100});
        QSize readySize;
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &s)
                         {
                             readySize = s;
                             ready = true;
                         });
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return ready; }, 60000), "C: 100MP opens");

        // Through resize / fullscreen / restore the LOD display must stay
        // alive and keep the full source geometry (fit correctness). Over-
        // dense rasters are fine (they still cover the viewport); an absent
        // raster or a wrong source size is the failure.
        auto assertAlive = [&](const char *what)
        {
            pump(800); // let any pending materialization settle
            const bool alive = viewer.isLodDisplay() && !viewer.displayRaster().isNull() &&
                               readySize == QSize(12000, 8333);
            printf("  C: %s viewport=%dx%d source=%dx%d lod=%d raster=%d\n", what,
                   viewer.width(), viewer.height(), readySize.width(), readySize.height(),
                   viewer.isLodDisplay() ? 1 : 0, viewer.displayRaster().isNull() ? 0 : 1);
            std::fflush(stdout);
            if (!alive)
            {
                ++g_failures;
                std::printf("FAIL: C: %s keeps a correct LOD fit\n", what);
            }
        };

        assertAlive("after-open");
        viewer.resize(640, 480);
        viewer.update();
        assertAlive("after-resize");
        viewer.setFullscreenRequested(true);
        viewer.update();
        assertAlive("after-fullscreen");
        viewer.setFullscreenRequested(false);
        viewer.resize(1280, 800);
        viewer.update();
        assertAlive("after-restore");
    }

    // ── D: Compare deep zoom reaches viewport/source density (RED) ───────────
    {
        MARK("D start");
        installDefaults();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({hf72, hf72});
        CHECK(waitTrue([&] { return comparePanesReady(&ws, 2); }, 60000),
              "D: 72MP panes display");
        RawImageView *v1 = paneView(&ws, 1);
        CHECK(v1 != nullptr, "D: pane 1 exists");
        if (v1)
        {
            // Six wheel steps -> ~1.15^6 = 2.31x zoom (deep).
            const QPointF center(v1->rect().center());
            for (int k = 0; k < 6; ++k)
            {
                QWheelEvent event(center, center, QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                                  Qt::NoModifier, Qt::NoScrollPhase, false);
                QApplication::sendEvent(v1, &event);
                pump(30);
            }
            pump(1500); // materialization settles
            const double scale = ws.engine().cellTransform(1).scale;
            const QSize pane = v1->image().size();
            const QSize src = v1->sourceSize();
            const double dpr = std::max(1.0, v1->devicePixelRatioF());
            // At deep zoom the visible source rect is src.width()/scale wide;
            // the pane raster must provide at least that many pixels (x dpr).
            const double neededWidth = static_cast<double>(src.width()) / scale * dpr;
            printf("  D: scale=%.2f pane raster=%dx%d neededWidth=%.0f (dpr=%.1f)\n", scale,
                   pane.width(), pane.height(), neededWidth, dpr);
            std::fflush(stdout);
            CHECK(static_cast<double>(pane.width()) >= neededWidth * 0.9,
                  "D: pane raster density matches the viewport at deep zoom "
                  "(RED today: whole-image LOD ceiling)");
        }
    }

    // ── E: source-backed placeholder metadata completeness (RED) ─────────────
    {
        MARK("E start");
        installDefaults();
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        ws.setImages({adobe100, adobe100});
        CHECK(waitTrue([&] { return comparePanesReady(&ws, 2); }, 60000),
              "E: panes display");
        const ImageFrame *frame = ws.engine().imageAt(0);
        CHECK(frame != nullptr, "E: placeholder frame exists");
        if (frame)
        {
            const auto &m = frame->metadata();
            printf("  E: fileName='%s' fileSize=%lld modified=%lld orientation=%d w=%d h=%d "
                   "icc=%zu\n",
                   m.fileName.c_str(), static_cast<long long>(m.fileSize),
                   static_cast<long long>(m.modifiedEpochSec), m.orientation, m.width, m.height,
                   m.textKeys.count("MViewer.DisplayICC.Base64"));
            std::fflush(stdout);
            CHECK(!m.fileName.empty(), "E: fileName is stable (RED today)");
            CHECK(m.fileSize > 0, "E: fileSize is stable (RED today)");
            CHECK(m.modifiedEpochSec > 0, "E: modified identity is stable (RED today)");
            CHECK(m.orientation == 1, "E: orientation is stable");
            CHECK(m.width == 12000 && m.height == 8333, "E: dims are stable");
            CHECK(m.textKeys.count("MViewer.DisplayICC.Base64") != 0,
                  "E: display ICC metadata is present on the placeholder (RED today)");
        }
    }

    // ── F1: a throwing probe must not escape CompareWorkspace::setImages (RED)
    {
        MARK("F1 start");
        installThrowing(ThrowingM48Decoder::Mode::Probe);
        CompareWorkspace ws;
        ws.resize(1280, 800);
        ws.show();
        bool threw = false;
        try
        {
            ws.setImages({throwingPath, jpeg100});
        }
        catch (...)
        {
            threw = true;
        }
        CHECK(!threw,
              "F1: a throwing probe never escapes setImages on the UI thread (RED today)");
        pump(1500);
        CHECK(waitTrue(schedulerIdle, 20000), "F1: pools drain");
    }

    // ── F2: a throwing decodeLod reaches an observable terminal (RED) ────────
    {
        MARK("F2 start");
        installThrowing(ThrowingM48Decoder::Mode::Lod);
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool failed = false;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return failed; }, 15000),
              "F2: a failed LOD decode reaches the loadFailed terminal (RED today)");
        CHECK(waitTrue(schedulerIdle, 20000), "F2: pools drain after the terminal");
        // Recovery: a subsequent valid open must work.
        installDefaults();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setBrowseSequence({jpeg100});
        viewer.setImage(jpeg100);
        CHECK(waitTrue([&] { return ready; }, 60000),
              "F2: the viewer recovers and opens a valid image afterwards");
    }

    // ── F3: a throwing decodeRegion keeps the raster and reaches a terminal ──
    {
        MARK("F3 start");
        installThrowing(ThrowingM48Decoder::Mode::Region);
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        bool ready = false;
        QObject::connect(&viewer, &ImageViewer::displayReady, &viewer,
                         [&](const QSize &) { ready = true; });
        viewer.setBrowseSequence({throwingPath});
        viewer.setImage(throwingPath);
        CHECK(waitTrue([&] { return ready; }, 60000), "F3: initial LOD displays");
        const QImage before = viewer.displayRaster();
        const int regionCallsBefore = g_throwing->regionCalls.load();
        bool failed = false;
        QObject::connect(&viewer, &ImageViewer::loadFailed, &viewer,
                         [&](const QString &) { failed = true; });
        viewer.zoomActual();
        viewer.update();
        CHECK(waitTrue([&] { return g_throwing->regionCalls.load() > regionCallsBefore; }, 30000),
              "F3: zoom requests a region raster");
        pump(1500);
        CHECK(!viewer.displayRaster().isNull() && viewer.displayRaster() == before,
              "F3: the current good raster is kept when the upgrade fails");
        CHECK(failed,
              "F3: the failed region upgrade reaches the loadFailed terminal (RED today)");
        CHECK(waitTrue(schedulerIdle, 20000), "F3: pools drain");
    }

    std::printf("=== M48 Phase 0 regressions: %s (RED cases must fail until fixed) ===\n",
                g_failures == 0 ? "PASS" : "FAIL");
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
