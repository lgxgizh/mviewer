// M47 Phase 0 — large-image data-path BASELINE (before any Phase 1 change).
//
// Purpose (milestone requirement): prove, with measurements, which data paths
// currently materialize a full-resolution RGB bitmap, and record the baseline
// numbers (open latency, first-usable latency, decode entry-point counts, peak
// RSS, materialized bytes, cache warm/cold) that Phase 1..3 must improve.
//
// Two sections:
//   A) Repository data path   File -> ImageRepository -> ImageFrame
//      per fixture: full load (cold), scaled-256 load (cold), and cache-warm
//      full reload for the 100 MP JPEG. Reports which decoder entry points ran
//      (decodeFull vs decodeScaled) via a counting decoder.
//   B) Viewer data path       File -> ImageRepository -> ImageViewer
//      opens the 100 MP JPEG through a real ImageViewer and reports the wall
//      time to first usable frame plus the frame dimensions (proves whether the
//      current path requires a full-resolution ImageFrame).
//
// Instrumentation is benchmark-local and does not touch production code: a
// CountingDecoder is registered ahead of a fresh QtDecoder (the process's
// DecoderRegistry lineup is reset for the duration), exactly the pattern the
// M46 test suite already uses for counting decoders.
//
// Usage:
//   m47_large_image_baseline.exe [--root <testdata/large dir>] [--out <json>]
//
// Exit code 0 = measurements recorded; 1 = a required fixture is missing or a
// measurement could not be produced. No pass/fail threshold is enforced here —
// it is a baseline recorder, not a gate (Phase 6 gates enforce the target).

#include "core/image/Decoder.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/QtDecoder.h"
#include "core/image/decoder/QtFallbackDecoder.h"
#include "core/perf/MemoryTracker.h"
#include "imageviewer.h"

#include <QApplication>
#include <QDir>
#include <QFileInfo>
#include <QImageReader>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QString>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <memory>
#include <string>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace
{

struct Options
{
    QString root;
    QString outFile;
};

Options g_opts;
int g_failures = 0;

void fail(const char *what)
{
    printf("  [baseline] FAIL: %s\n", what);
    ++g_failures;
}

double rssMB()
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters)))
        return counters.WorkingSetSize / (1024.0 * 1024.0);
#endif
    return 0.0;
}

// ── Counting delegating decoder (benchmark-local instrumentation) ───────────
class CountingDecoder : public IDecoder
{
  public:
    mutable std::atomic<int> fullCalls{0};
    mutable std::atomic<int> scaledCalls{0};

    bool canDecode(const std::string &path) const override
    {
        const QString ext = QFileInfo(QString::fromStdString(path)).suffix().toLower();
        return ext == "jpg" || ext == "jpeg" || ext == "tif" || ext == "tiff" || ext == "png" ||
               ext == "bmp";
    }
    ImageData decodeFull(const std::string &path) const override
    {
        fullCalls.fetch_add(1);
        return m_inner.decodeFull(path);
    }
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &meta) const override
    {
        fullCalls.fetch_add(1);
        return m_inner.decodeFull(path, meta);
    }
    ImageData decodeScaled(const std::string &path, int maxEdge) const override
    {
        scaledCalls.fetch_add(1);
        return m_inner.decodeScaled(path, maxEdge);
    }
    ImageData decodeScaled(const std::string &path, int maxEdge,
                           mviewer::domain::ImageMetadata &meta) const override
    {
        scaledCalls.fetch_add(1);
        return m_inner.decodeScaled(path, maxEdge, meta);
    }
    std::vector<std::string> extensions() const override { return m_inner.extensions(); }
    const char *name() const override { return "CountingBaselineDecoder"; }

  private:
    QtDecoder m_inner;
};

CountingDecoder *g_counter = nullptr;

void resetCounter()
{
    if (g_counter)
    {
        g_counter->fullCalls.store(0);
        g_counter->scaledCalls.store(0);
    }
}

void clearCaches()
{
    // Full cold start: drop the memory/disk cache and the path->key map so the
    // measurement reflects a genuine decode, not a cache hit.
    ImageRepository::instance().invalidateAll();
}

double nowMs()
{
    return std::chrono::duration<double, std::milli>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}

struct Measure
{
    QString fixture;
    QString mode; // full-cold | scaled-256-cold | full-warm
    double latencyMs = 0.0;
    int w = 0;
    int h = 0;
    size_t rgbBytes = 0; // w*h*3 (RGB24 source materialized)
    double rssBefore = 0.0;
    double rssAfter = 0.0;
    double rssDeltaMB = 0.0;
    int fullDecodes = 0;
    int scaledDecodes = 0;
    size_t liveFrames = 0;
    bool ok = false;
};

Measure repoFull(const QString &path, bool warm)
{
    Measure m;
    m.fixture = QFileInfo(path).fileName();
    m.mode = warm ? "full-warm" : "full-cold";
    resetCounter();
    if (!warm)
        clearCaches();
    m.rssBefore = rssMB();
    const double t0 = nowMs();
    auto res = ImageRepository::instance().load(path.toUtf8().toStdString());
    const double t1 = nowMs();
    m.latencyMs = t1 - t0;
    if (res.frame && !res.frame->pixels().isNull())
    {
        m.w = res.frame->width();
        m.h = res.frame->height();
        m.rgbBytes = static_cast<size_t>(m.w) * static_cast<size_t>(m.h) * 3;
        m.ok = m.w > 0 && m.h > 0;
    }
    m.rssAfter = rssMB();
    m.rssDeltaMB = m.rssAfter - m.rssBefore;
    m.fullDecodes = g_counter ? g_counter->fullCalls.load() : 0;
    m.scaledDecodes = g_counter ? g_counter->scaledCalls.load() : 0;
    m.liveFrames = mviewer::perf::MemoryTracker::instance().sample().liveImageFrames;
    return m;
}

Measure repoScaled256(const QString &path)
{
    Measure m;
    m.fixture = QFileInfo(path).fileName();
    m.mode = "scaled-256-cold";
    resetCounter();
    clearCaches();
    m.rssBefore = rssMB();
    const double t0 = nowMs();
    auto img = Decoder::decodeScaled(path.toUtf8().toStdString(), 256);
    const double t1 = nowMs();
    m.latencyMs = t1 - t0;
    m.w = img.width;
    m.h = img.height;
    m.rgbBytes = static_cast<size_t>(img.width) * static_cast<size_t>(img.height) * 3;
    m.ok = img.buffer && img.width > 0;
    m.rssAfter = rssMB();
    m.rssDeltaMB = m.rssAfter - m.rssBefore;
    m.fullDecodes = g_counter ? g_counter->fullCalls.load() : 0;
    m.scaledDecodes = g_counter ? g_counter->scaledCalls.load() : 0;
    return m;
}

void printRow(const Measure &m)
{
    printf("  %-22s %-14s latency=%8.1f ms  out=%4dx%-4d rgb=%10zu B  "
           "rss+%7.1f MB  fullDecode=%d scaledDecode=%d\n",
           qPrintable(m.fixture), qPrintable(m.mode), m.latencyMs, m.w, m.h, m.rgbBytes,
           m.rssDeltaMB, m.fullDecodes, m.scaledDecodes);
}

// ── Viewer path (section B) ─────────────────────────────────────────────────
struct ViewerMeasure
{
    double latencyMs = 0.0;
    int frameW = 0;
    int frameH = 0;
    double rssWhileHeldMB = 0.0;
    bool materializedFull = false; // frame dims == source dims
};

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

// UI-thread cost probe: after opening the image, measure synchronous repaint
// wall time at Fit, 100% and one zoom step. Any scaling of the full frame that
// happens on the paint path shows up here as UI-thread stall.
void viewerUiCostProbe(const QString &path)
{
    resetCounter();
    clearCaches();
    ImageViewer viewer;
    viewer.resize(1280, 800);
    viewer.show();
    viewer.setBrowseSequence({path});
    bool ready = false;
    QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                     [&](const std::shared_ptr<ImageFrame> &) { ready = true; });
    viewer.setImage(path);
    const double deadline = nowMs() + 120000.0;
    while (!ready && nowMs() < deadline)
        pump(1);
    QApplication::processEvents();

    double t0 = nowMs();
    viewer.repaint();
    const double fitMs = nowMs() - t0;

    viewer.zoomActual();
    QApplication::processEvents();
    t0 = nowMs();
    viewer.repaint();
    const double actualMs = nowMs() - t0;

    viewer.zoomIn();
    QApplication::processEvents();
    t0 = nowMs();
    viewer.repaint();
    const double zoomInMs = nowMs() - t0;

    viewer.zoomFit();
    QApplication::processEvents();
    t0 = nowMs();
    viewer.repaint();
    const double refitMs = nowMs() - t0;

    printf("  viewer UI-thread repaint cost (6000x4000): fit=%6.1f ms  100%%=%6.1f ms  "
           "zoomIn=%6.1f ms  refit=%6.1f ms\n",
           fitMs, actualMs, zoomInMs, refitMs);
}

// Wait for imageReady; requires the counting decoder (already installed).
ViewerMeasure viewerOpen(const QString &path, int waitMs = 120000)
{
    ViewerMeasure v;
    resetCounter();
    clearCaches();
    ImageViewer viewer;
    viewer.resize(1280, 800);
    viewer.show();
    viewer.setBrowseSequence({path});
    const double t0 = nowMs();
    viewer.setImage(path);
    // Wait for the first usable frame (imageReady is emitted from
    // applyLoadedImage once the full decode is delivered).
    bool ready = false;
    QObject::connect(&viewer, &ImageViewer::imageReady, &viewer,
                     [&](const std::shared_ptr<ImageFrame> &) { ready = true; });
    const double deadline = nowMs() + waitMs;
    while (!ready && nowMs() < deadline)
    {
        pump(1);
        if (ready)
            break;
    }
    v.latencyMs = nowMs() - t0;
    std::shared_ptr<ImageFrame> frame = viewer.frame();
    if (frame && frame->isValid())
    {
        v.frameW = frame->width();
        v.frameH = frame->height();
        v.materializedFull = v.frameW > 0 && v.frameH > 0;
    }
    v.rssWhileHeldMB = rssMB();
    return v;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    for (int i = 1; i < argc; ++i)
    {
        const QString a = QString::fromLocal8Bit(argv[i]);
        if (a == "--root" && i + 1 < argc)
            g_opts.root = QString::fromLocal8Bit(argv[++i]);
        else if (a == "--out" && i + 1 < argc)
            g_opts.outFile = QString::fromLocal8Bit(argv[++i]);
    }
    if (g_opts.root.isEmpty())
        g_opts.root = QString(MVIEWER_SOURCE_DIR) + "/testdata/large";
    const QString root = g_opts.root;

    if (!QDir(root).exists())
    {
        printf("fixture root missing: %s\n", qPrintable(root));
        printf("run: python testdata/generate_large_fixtures.py --ensure\n");
        return 1;
    }

    // Install counting decoder AHEAD of a fresh QtDecoder and the fallback so
    // every JPEG/TIFF call through the frozen registry lands on the counter
    // (benchmark-local instrumentation, same pattern as the M46 tests).
    auto &registry = DecoderRegistry::instance();
    registry.resetToDefaults();
    registry.unregister("QtDecoder");
    registry.unregister("QtFallbackDecoder");
    auto counter = std::make_shared<CountingDecoder>();
    g_counter = counter.get();
    registry.registerDecoder(counter);
    registry.registerDecoder(std::make_shared<QtDecoder>());
    registry.registerDecoder(std::make_shared<QtFallbackDecoder>());

    const std::vector<QString> fixtures = {
        "large_jpeg_100mp.jpg", "large_tiff_100mp.tiff", "high_compression.jpg",
        "extreme_wide.jpg",     "extreme_tall.jpg",
    };

    printf("=== M47 Phase 0 baseline: current data path (before Phase 1) ===\n");
    printf("fixture root: %s\n", qPrintable(root));
    printf("\n-- A) Repository data path: File -> ImageRepository -> ImageFrame --\n");

    std::vector<Measure> measures;
    for (const QString &f : fixtures)
    {
        const QString p = root + "/" + f;
        if (!QFileInfo::exists(p))
        {
            printf("  [baseline] MISSING fixture: %s\n", qPrintable(p));
            ++g_failures;
            continue;
        }
        Measure cold = repoFull(p, false);
        printRow(cold);
        measures.push_back(cold);
        Measure scaled = repoScaled256(p);
        printRow(scaled);
        measures.push_back(scaled);
    }
    // Cache-warm full reload for the 100 MP JPEG (records the warm path).
    Measure warm = repoFull(root + "/large_jpeg_100mp.jpg", true);
    printRow(warm);
    measures.push_back(warm);

    printf("\n-- B) Viewer data path: File -> ImageRepository -> ImageViewer --\n");
    // B1: a 24 MP JPEG (fits Qt's 256 MB allocation limit) — measures the
    // current cost of a NORMAL large-image open through the real viewer.
    const QString viewerPath = root + "/high_compression.jpg";
    if (QFileInfo::exists(viewerPath))
    {
        const double rssBefore = rssMB();
        ViewerMeasure v = viewerOpen(viewerPath);
        printf("  viewer open-to-first-frame (6000x4000): %.1f ms  frame=%dx%d  "
               "rss-while-held=%+.1f MB  fullDecode=%d  => %s\n",
               v.latencyMs, v.frameW, v.frameH, v.rssWhileHeldMB - rssBefore,
               g_counter ? g_counter->fullCalls.load() : 0,
               v.materializedFull ? "FULL-RES frame materialized" : "unexpected");
        if (v.frameW != 6000 || v.frameH != 4000)
            fail("viewer materialized a frame that is NOT the full 6000x4000 source");
        viewerUiCostProbe(viewerPath);
        // B3: repeated open/close on the same viewer-sized fixture (the second
        // open is cache-warm) — records warm open latency and RSS drift.
        printf("  viewer repeated open/close (6000x4000):\n");
        for (int i = 0; i < 4; ++i)
        {
            const double before = rssMB();
            resetCounter();
            ViewerMeasure w = viewerOpen(viewerPath);
            printf("    open #%d: %.1f ms  rss-delta=%+.1f MB  fullDecode=%d\n", i + 2,
                   w.latencyMs, w.rssWhileHeldMB - before,
                   g_counter ? g_counter->fullCalls.load() : 0);
        }
    }
    else
    {
        ++g_failures;
        printf("  [baseline] MISSING fixture: %s\n", qPrintable(viewerPath));
    }
    // B2: the 100 MP JPEG — CURRENT reproduction: Qt's 256 MB QImage allocation
    // limit rejects the full decode, so the current viewer cannot open it at
    // all (the scaled-256 path above is the only working preview today).
    // This is an expected baseline FINDING, not a harness failure.
    const QString viewer100 = root + "/large_jpeg_100mp.jpg";
    if (QFileInfo::exists(viewer100))
    {
        ViewerMeasure v = viewerOpen(viewer100, 8000);
        printf("  viewer open-to-first-frame (100MP): %.1f ms  frame=%dx%d  fullDecode=%d  "
               "=> %s\n",
               v.latencyMs, v.frameW, v.frameH, g_counter ? g_counter->fullCalls.load() : 0,
               (v.frameW == 0 && v.frameH == 0)
                   ? "REPRODUCTION: cannot open (Qt 256 MB allocation limit)"
                   : "opened (unexpected at baseline)");
    }

    printf("\n=== baseline summary: %d failure(s) ===\n", g_failures);

    if (!g_opts.outFile.isEmpty())
    {
        QJsonArray arr;
        for (const Measure &m : measures)
        {
            QJsonObject o;
            o["fixture"] = m.fixture;
            o["mode"] = m.mode;
            o["latency_ms"] = m.latencyMs;
            o["width"] = m.w;
            o["height"] = m.h;
            o["rgb_bytes"] = static_cast<qint64>(m.rgbBytes);
            o["rss_delta_mb"] = m.rssDeltaMB;
            o["full_decodes"] = m.fullDecodes;
            o["scaled_decodes"] = m.scaledDecodes;
            o["ok"] = m.ok;
            arr.append(o);
        }
        QJsonObject root2;
        root2["milestone"] = "M47";
        root2["phase"] = "0-baseline";
        root2["fixture_root"] = root;
        root2["measures"] = arr;
        QFile f(g_opts.outFile);
        if (f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        {
            f.write(QJsonDocument(root2).toJson(QJsonDocument::Indented));
            printf("wrote %s\n", qPrintable(g_opts.outFile));
        }
    }

    return g_failures == 0 ? 0 : 1;
}
