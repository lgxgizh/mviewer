// M46 — Browse real-world reliability regressions (offscreen UI tests).
//
// Deterministic coverage for the Browse hot path:
//
//   B1  DetailsDelegate paint performs NO filesystem metadata query: after the
//       scan, every source file is DELETED and the view is repainted — the
//       render must be pixel-identical to the pre-deletion render because all
//       painted values (size/mtime/format) come from scan-cached Entry data.
//       (Pre-fix: QFileInfo::size()/lastModified() inside paint() showed
//       "0 B"/"-" after deletion — the renders diverged.)
//   B2  Same guarantee for the Thumbnail grid delegate footer text.
//   B3  A superseded directory scan stops cooperatively: with the scan
//       iteration probe installed, switching A→B while A is walking bounds the
//       number of A iterations after the switch to ≤ 1 (the in-flight one).
//   B4  A superseded dimension probe (Details mode) stops cooperatively:
//       bounded iterations after the directory switch, and B's entries land.
//   B5  BusyCursor ownership: queued scans cleared by destruction cannot leave
//       the app-global override cursor set; running scans restore exactly once.
//   B6  ImageViewer destroyed mid-decode (deterministic latch): pools converge,
//       no callback touches the viewer.
//   B7  ImageViewer A→B→A with in-flight decodes: newest generation only.
//   B8  CompareWorkspace rapid setImages swap then destroy: converges cleanly.

#include "compareworkspace.h"
#include "core/image/ImageRepository.h"
#include "core/image/decoder/DecoderRegistry.h"
#include "core/image/decoder/IDecoder.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "imageviewer.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QCursor>
#include <QDir>
#include <QImage>
#include <QThread>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

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
        fflush(stdout);                                                                            \
    } while (0)

namespace
{
using PoolType = TaskScheduler::PoolType;
using Priority = TaskScheduler::Priority;
using TaskContext = TaskScheduler::TaskContext;

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

bool waitEntryCount(ThumbnailPanel &panel, int expected, int ms)
{
    return waitTrue([&] { return panel.entries().size() == expected; }, ms);
}

QString makeImageDir(const QString &tag, int count, int w = 8, int h = 8)
{
    const QString dir = QDir::tempPath() + "/mviewer_m46_" + tag + "_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    for (int i = 0; i < count; ++i)
    {
        QImage img(w, h, QImage::Format_RGB32);
        img.fill(0xFF000000 + ((i * 2654435761u) & 0xFFFFFF));
        img.save(dir + QString("/img_%1.png").arg(i, 5, 10, QChar('0')), "PNG");
    }
    return dir;
}

bool poolsConverged()
{
    for (int p = 0; p < 5; ++p)
    {
        const auto m = TaskScheduler::instance().metrics(static_cast<PoolType>(p));
        if (m.pending != 0 || m.active_tasks != 0 || m.queue_depth != 0 || m.waiting != 0)
            return false;
    }
    return TaskScheduler::instance().graphMetrics().handles == 0;
}

// Test-only decoder: counts calls, signals "entered", blocks until released.
struct DecodeControl
{
    std::atomic<int> calls{0};
    std::atomic<bool> entered{false};
    std::atomic<bool> release{false};
};

class BlockingCountingDecoder : public IDecoder
{
  public:
    explicit BlockingCountingDecoder(std::shared_ptr<DecodeControl> ctl) : m_ctl(std::move(ctl))
    {
    }

    bool canDecode(const std::string &path) const override
    {
        const std::string ext = ".mvtest";
        return path.size() > ext.size() &&
               path.compare(path.size() - ext.size(), ext.size(), ext) == 0;
    }

    ImageData decodeFull(const std::string &path) const override
    {
        mviewer::domain::ImageMetadata meta;
        return decodeFull(path, meta);
    }

    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override
    {
        m_ctl->calls.fetch_add(1, std::memory_order_relaxed);
        m_ctl->entered.store(true, std::memory_order_release);
        while (!m_ctl->release.load(std::memory_order_acquire))
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        outMeta.width = 16;
        outMeta.height = 16;
        outMeta.channels = 3;
        outMeta.bitDepth = 8;
        outMeta.format = "TEST";
        outMeta.filePath = path;
        return makeImageData(16, 16, PixelFormat::RGB24);
    }

    ImageData decodeScaled(const std::string &path, int maxEdge) const override
    {
        (void)maxEdge;
        mviewer::domain::ImageMetadata meta;
        return decodeFull(path, meta);
    }

    std::vector<std::string> extensions() const override
    {
        return {"mvtest"};
    }

    const char *name() const override
    {
        return "BlockingCountingTestDecoder";
    }

  private:
    std::shared_ptr<DecodeControl> m_ctl;
};

void writeDummyFile(const std::string &path)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (f)
    {
        std::fwrite("mvtest", 1, 6, f);
        std::fclose(f);
    }
}

void cleanupDir(const QString &dir)
{
    QDir(dir).removeRecursively();
}

// ─── B1/B2: paint renders identical after the source files are deleted ──────
// Wait until the shared pipeline is idle (no in-flight or queued thumbnail
// work) so no late delivery can change the ready-pixmap set between the two
// renders — the comparison is deterministic.
bool waitPipelineIdle(int ms)
{
    return waitTrue(
        []()
        {
            return ThumbnailPipeline::instance().handlesCount() == 0 &&
                   ThumbnailPipeline::instance().pendingCount() == 0;
        },
        ms);
}

void testPaintNeverStats()
{
    printf("\n[B1/B2. paint path renders cached values after files are deleted]\n");
    {
        const QString dir = makeImageDir("paint_details", 60);
        ThumbnailPanel panel;
        panel.resize(1000, 700);
        panel.setViewMode(ThumbnailPanel::Details);
        panel.show();
        pump(100);
        panel.setDirectory(dir);
        CHECK(waitEntryCount(panel, 60, 15000), "B1: details scan populated");
        CHECK(waitPipelineIdle(20000), "B1: thumbnail pipeline idle before the renders");
        // Double-check: the visible range is scheduled via a singleShot(0)
        // timer; confirm the idle state is stable (no late submission) before
        // capturing the reference render.
        pump(150);
        CHECK(waitPipelineIdle(5000), "B1: pipeline stays idle (no late submission)");
        pump(200); // let dimension probes and any async work settle

        const QImage before = panel.grab().toImage();
        CHECK(!before.isNull(), "B1: pre-deletion render captured");

        // Delete every source file: a paint path that stat()s the files would
        // now render size 0 / invalid mtime (values change); the cached-entry
        // paint path renders byte-identical output.
        QDir(dir).removeRecursively();
        pump(500);
        const QImage after = panel.grab().toImage();
        CHECK(!after.isNull(), "B1: post-deletion render captured");
        CHECK(before == after, "B1: details render is pixel-identical after file deletion "
                               "(paint reads scan-cached entries only)");
        panel.hide();
    }
    {
        const QString dir = makeImageDir("paint_thumb", 60, 32, 24);
        ThumbnailPanel panel;
        panel.resize(1000, 700);
        panel.setViewMode(ThumbnailPanel::Thumbnail);
        panel.show();
        pump(100);
        panel.setDirectory(dir);
        CHECK(waitEntryCount(panel, 60, 15000), "B2: thumb scan populated");
        CHECK(waitPipelineIdle(20000), "B2: thumbnail pipeline idle before the renders");
        pump(150);
        CHECK(waitPipelineIdle(5000), "B2: pipeline stays idle (no late submission)");
        // At least one thumbnail must be ready so the footer text and image
        // are actually painted in both renders.
        const QString firstPath = panel.pathList().value(0);
        CHECK(!panel.thumbReady(firstPath).isNull(),
              "B2: at least one ready thumbnail before the render comparison");
        panel.setFilter(QStringLiteral("__mviewer_filter_no_match__"));
        CHECK(panel.pathList().isEmpty(), "B2: no-match filter clears the visible rows");
        panel.setFilter({});
        pump(50);
        CHECK(panel.pathList().size() == 60,
              "B2: clearing the no-match filter restores all rows");
        CHECK(!panel.thumbReady(firstPath).isNull(),
              "B2: no-match filter rebuild preserves an already-decoded thumbnail");
        const QImage before = panel.grab().toImage();
        QDir(dir).removeRecursively();
        pump(500);
        const QImage after = panel.grab().toImage();
        CHECK(before == after,
              "B2: thumbnail-grid render is pixel-identical after file deletion");
        panel.hide();
    }
    pump(500);
    CHECK(QApplication::overrideCursor() == nullptr, "B1/B2: no stuck busy cursor");
}

// ─── B3: superseded directory scan stops cooperatively ──────────────────────
void testScanSupersessionBounded()
{
    printf("\n[B3. superseded directory scan stops cooperatively]\n");
    const QString dirA = makeImageDir("scan_a", 3000);
    const QString dirB = makeImageDir("scan_b", 300);

    struct ProbeState
    {
        std::shared_ptr<std::atomic<uint64_t>> token;
        std::mutex mtx;
        std::vector<uint64_t> seen;
        std::atomic<bool> anyCall{false};
        uint64_t genA = 0;
    };
    auto ps = std::make_shared<ProbeState>();
    {
        ThumbnailPanel panel;
        panel.resize(900, 600);
        panel.show();
        pump(100);
        ps->token = panel.scanGenTokenForTest();
        ps->genA = 1; // first setDirectory bumps m_dirGen 0 -> 1

        ThumbnailPanel::setScanIterationProbe([ps]()
                                              {
                                                  ps->anyCall.store(true, std::memory_order_release);
                                                  std::lock_guard<std::mutex> lk(ps->mtx);
                                                  ps->seen.push_back(
                                                      ps->token->load(std::memory_order_acquire));
                                              });
        panel.setDirectory(dirA);
        CHECK(waitTrue([&] { return ps->anyCall.load(); }, 15000),
              "B3: scan A started (probe observed)");
        const auto countA = [&]()
        {
            std::lock_guard<std::mutex> lk(ps->mtx);
            size_t n = 0;
            for (uint64_t g : ps->seen)
                if (g == ps->genA)
                    ++n;
            return n;
        };
        const size_t before = countA();
        panel.setDirectory(dirB);
        CHECK(waitEntryCount(panel, 300, 15000), "B3: directory B completed");
        const size_t after = countA();
        CHECK(after - before <= 1,
              "B3: superseded scan A processed at most the in-flight iteration "
              "after the switch (bounded stale work)");
        bool allB = true;
        for (const auto &e : panel.entries())
            if (!e.path.startsWith(dirB, Qt::CaseInsensitive))
                allB = false;
        CHECK(allB, "B3: final model holds only directory B");
        ThumbnailPanel::setScanIterationProbe({});
        panel.hide();
    }
    pump(1000);
    CHECK(QApplication::overrideCursor() == nullptr, "B3: busy cursor balanced");
    QDir(dirA).removeRecursively();
    QDir(dirB).removeRecursively();
}

// ─── B4: superseded dimension probe stops cooperatively ─────────────────────
void testDimensionSupersessionBounded()
{
    printf("\n[B4. superseded dimension probe stops cooperatively]\n");
    const QString dirA = makeImageDir("dim_a", 1500, 16, 16);
    const QString dirB = makeImageDir("dim_b", 150);

    struct ProbeState
    {
        std::shared_ptr<std::atomic<uint64_t>> token;
        std::mutex mtx;
        std::vector<uint64_t> seen;
        std::atomic<bool> armBlock{false};
        uint64_t genA = 0;
    };
    auto ps = std::make_shared<ProbeState>();
    {
        ThumbnailPanel panel;
        panel.resize(900, 600);
        panel.setViewMode(ThumbnailPanel::Details); // triggers ensureDimensions
        panel.show();
        pump(100);
        ps->token = panel.scanGenTokenForTest();
        ps->genA = 1;
        // Deterministic slow probe: once armed it sleeps per iteration, which
        // stretches the 1500-file dimension pass over seconds so the directory
        // switch deterministically lands mid-probe. The assertion is on CALL
        // COUNTS, not timing: after the switch, the superseded pass may finish
        // only the iteration that was already running.
        ThumbnailPanel::setScanIterationProbe([ps]()
                                              {
                                                  if (ps->armBlock.load(std::memory_order_acquire))
                                                      std::this_thread::sleep_for(
                                                          std::chrono::milliseconds(2));
                                                  std::lock_guard<std::mutex> lk(ps->mtx);
                                                  ps->seen.push_back(
                                                      ps->token->load(std::memory_order_acquire));
                                              });
        ps->armBlock.store(true, std::memory_order_release);
        panel.setDirectory(dirA);
        CHECK(waitEntryCount(panel, 1500, 60000), "B4: scan A completed (dimension pass running)");
        const auto countA = [&]()
        {
            std::lock_guard<std::mutex> lk(ps->mtx);
            size_t n = 0;
            for (uint64_t g : ps->seen)
                if (g == ps->genA)
                    ++n;
            return n;
        };
        const size_t before = countA();
        panel.setDirectory(dirB);
        CHECK(waitEntryCount(panel, 150, 60000), "B4: directory B completed");
        const size_t after = countA();
        CHECK(after - before <= 1,
              "B4: superseded dimension probe ran at most the in-flight iteration "
              "(stale header probing stops cooperatively)");
        ThumbnailPanel::setScanIterationProbe({});
        panel.hide();
    }
    pump(1000);
    CHECK(QApplication::overrideCursor() == nullptr, "B4: busy cursor balanced");
    QDir(dirA).removeRecursively();
    QDir(dirB).removeRecursively();
}

// ─── B5: busy cursor ownership with queued scans dropped by destruction ─────
void testBusyCursorOwnership()
{
    printf("\n[B5. busy cursor balanced when queued scans are dropped]\n");
    struct ProbeState
    {
        std::atomic<bool> armBlock{false};
        std::atomic<bool> releaseBlock{false};
        std::atomic<int> blockedCalls{0};
    };
    auto ps = std::make_shared<ProbeState>();
    ThumbnailPanel::setScanIterationProbe([ps]()
                                          {
                                              if (ps->armBlock.load(std::memory_order_acquire))
                                              {
                                                  ps->blockedCalls.fetch_add(1);
                                                  while (!ps->releaseBlock.load(
                                                      std::memory_order_acquire))
                                                      std::this_thread::sleep_for(
                                                          std::chrono::milliseconds(1));
                                              }
                                          });
    const QString dirA = makeImageDir("cursor_a", 800);
    const QString dirB = makeImageDir("cursor_b", 800);
    const QString dirC = makeImageDir("cursor_c", 800);
    {
        ThumbnailPanel panel;
        panel.resize(900, 600);
        panel.show();
        pump(100);
        ps->armBlock.store(true, std::memory_order_release);
        panel.setDirectory(dirA); // scan A starts and blocks in the probe
        CHECK(waitTrue([&] { return ps->blockedCalls.load() >= 1; }, 10000),
              "B5: scan A blocked in-flight (pool worker occupied)");
        // B and C queue behind the blocked worker.
        panel.setDirectory(dirB);
        panel.setDirectory(dirC);
        pump(50);
        CHECK(QApplication::overrideCursor() != nullptr, "B5: busy cursor active while scanning");
        // Release scan A BEFORE destruction: its generation check then aborts
        // it (superseded by C), while B and C stay queued and are dropped by
        // the pool clear in the destructor — their busy-cursor refs must be
        // drained by the destructor, never stranded.
        ps->releaseBlock.store(true, std::memory_order_release);
        pump(300);
        // Destruction clears the QUEUED scans (B, C) — their refs must be
        // drained by the destructor.
    }
    pump(2000);
    CHECK(QApplication::overrideCursor() == nullptr,
          "B5: no stuck override cursor after queued scans were dropped");
    ThumbnailPanel::setScanIterationProbe({});
    QDir(dirA).removeRecursively();
    QDir(dirB).removeRecursively();
    QDir(dirC).removeRecursively();
}

// ─── B6: ImageViewer destroyed mid-decode (deterministic) ───────────────────
void testViewerDestroyMidDecode()
{
    printf("\n[B6. ImageViewer destroyed mid-decode]\n");
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "B6: DecodePool drained before test");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const QString path = tmp.path() + "/img_0.mvtest";
    writeDummyFile(path.toStdString());

    {
        ImageViewer viewer;
        viewer.resize(400, 300);
        viewer.setImage(path);
        CHECK(waitTrue([&] { return ctl->entered.load(); }, 5000),
              "B6: decode running when the viewer is destroyed");
        // viewer destroyed here — the token is invalidated and the request
        // cancelled while the decode is still in flight.
    }
    ctl->release.store(true, std::memory_order_release);
    CHECK(waitTrue(poolsConverged, 15000),
          "B6: pools converge after viewer destroyed mid-decode");
    CHECK(ctl->calls.load() == 1, "B6: decode ran exactly once");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "B6: DecodePool drains");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path());
    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));
}

// ─── B7: ImageViewer A -> B -> A ────────────────────────────────────────────
void testViewerABA()
{
    printf("\n[B7. ImageViewer A->B->A: newest generation only]\n");
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "B7: DecodePool drained before test");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const QString pathA = tmp.path() + "/img_a.mvtest";
    const QString pathB = tmp.path() + "/img_b.mvtest";
    writeDummyFile(pathA.toStdString());
    writeDummyFile(pathB.toStdString());

    {
        ImageViewer viewer;
        viewer.resize(400, 300);
        viewer.setImage(pathA); // decode A1 starts, blocks on the gate
        CHECK(waitTrue([&] { return ctl->calls.load() >= 1; }, 5000),
              "B7: decode A1 running");
        viewer.setImage(pathB); // supersedes A1 (cancel), B queued behind A1
        viewer.setImage(pathA); // supersedes B, A2 queued last — newest wins

        // Release the gate: A1 finishes (cancelled -> dropped), B finishes
        // (cancelled -> dropped), A2 (current generation) delivers. The
        // single-threaded DecodePool serializes all three decodes.
        ctl->release.store(true, std::memory_order_release);
        CHECK(waitTrue([&]
                       {
                           auto f = viewer.frame();
                           return f && !f->metadata().filePath.empty();
                       },
                       10000),
              "B7: viewer delivered a frame");
        const auto f = viewer.frame();
        CHECK(f && f->metadata().filePath == pathA.toStdString(),
              "B7: the FINAL A request owns the displayed frame");
        // A1 ran to completion (a running QImage decode is non-interruptible)
        // and warmed the FullImage cache. B was cancelled while still QUEUED,
        // so its work lambda exits before decoding, and the final A2 is served
        // from the warm cache — exactly one decoder invocation total.
        CHECK(ctl->calls.load() == 1,
              "B7: exactly one decode ran (A1); B was cancelled-queued, A2 was cache-served");
        CHECK(waitTrue(poolsConverged, 15000), "B7: pools converge after A->B->A");
        CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)),
              "B7: DecodePool drains");
    }
    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path());
    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));
}

// ─── B8: CompareWorkspace swap then destroy ─────────────────────────────────
void testCompareSwapThenDestroy()
{
    printf("\n[B8. CompareWorkspace rapid swap then destroy]\n");
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "B8: DecodePool drained before test");

    auto ctl = std::make_shared<DecodeControl>();
    DecoderRegistry::instance().registerDecoder(std::make_shared<BlockingCountingDecoder>(ctl));

    QTemporaryDir tmp;
    tmp.setAutoRemove(false);
    const QString a = tmp.path() + "/c_a.mvtest";
    const QString b = tmp.path() + "/c_b.mvtest";
    const QString c = tmp.path() + "/c_c.mvtest";
    const QString d = tmp.path() + "/c_d.mvtest";
    writeDummyFile(a.toStdString());
    writeDummyFile(b.toStdString());
    writeDummyFile(c.toStdString());
    writeDummyFile(d.toStdString());

    {
        CompareWorkspace ws;
        ws.resize(900, 600);
        ws.setImages({a, b});
        CHECK(waitTrue([&] { return ctl->calls.load() >= 1; }, 5000),
              "B8: first compare batch decoding");
        ws.setImages({c, d}); // supersede the in-flight batch (queued behind it)
        // Destroyed while the first batch is still decoding and the second is
        // queued — the destructor invalidates the lifetime token and cancels
        // both batches.
    }
    ctl->release.store(true, std::memory_order_release);
    CHECK(waitTrue(poolsConverged, 15000),
          "B8: pools converge after compare swap-then-destroy");
    CHECK(ctl->calls.load() == 1,
          "B8: only the in-flight first decode actually ran; every superseded/"
          "cancelled submission stopped before decoding");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(5)), "B8: DecodePool drains");

    DecoderRegistry::instance().unregister("BlockingCountingTestDecoder");
    cleanupDir(tmp.path());
    sched.setQueueMaxThreads(Priority::Decode, qMax(1, QThread::idealThreadCount()));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    printf("=== M46 Browse real-world reliability tests ===\n");
    fflush(stdout);

    testPaintNeverStats();
    testScanSupersessionBounded();
    testDimensionSupersessionBounded();
    testBusyCursorOwnership();
    testViewerDestroyMidDecode();
    testViewerABA();
    testCompareSwapThenDestroy();

    pump(1000);
    if (QApplication::overrideCursor() != nullptr)
    {
        // Defensive: never leave the test process with a stuck cursor.
        while (QApplication::overrideCursor())
            QApplication::restoreOverrideCursor();
        CHECK(false, "no override cursor may remain at test end");
    }

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
