// M27 Phase 4 — QObject async lifetime closure regression tests (UI level).
//
// Contracts under test (must hold after Phase 4 hardening):
//   1. A PreviewPanel destroyed while its decode is still queued/running must
//      never receive a callback (no invokeMethod on a dangling target, no
//      dereference of freed state).
//   2. Same for ImageViewer (guard checked BEFORE any use — including the
//      invokeMethod target).
//   3. A -> B -> A navigation: only the NEWEST request may deliver; older
//      generations (even of the same path) are dropped.
//   4. Destroy-mid-decode leaves the scheduler clean (pools converge).
//
// The destroy-mid-decode scenarios run in CHILD processes: pre-fix they crash
// or corrupt (the worker callback posts to a dangling `this`), so the parent
// observes the child's non-zero exit and reports FAIL. Post-fix the child
// exits 0 cleanly.

#include "imageviewer.h"
#include "previewpanel.h"

#include "core/cache/CacheManager.h"
#include "core/image/ImageRepository.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QProcess>
#include <QThread>
#include <QWidget>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <string>
#include <thread>

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

namespace
{
using Priority = TaskScheduler::Priority;
using PoolType = TaskScheduler::PoolType;
using TaskContext = TaskScheduler::TaskContext;

QString makeImageDir(const QString &tag, int count)
{
    const QString dir = QDir::tempPath() + "/mviewer_m27_" + tag + "_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    for (int i = 0; i < count; ++i)
    {
        QImage img(64, 64, QImage::Format_RGB32);
        img.fill(
            QRgb(0xFF000000 | ((i * 7) % 256) << 16 | ((i * 13) % 256) << 8 | ((i * 29) % 256)));
        img.save(dir + QString("/img_%1.png").arg(i, 5, 10, QChar('0')), "PNG");
    }
    return dir;
}

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QCoreApplication::processEvents(QEventLoop::AllEvents, 1);
}

volatile std::uint64_t g_churnSink = 0;
void churnStack()
{
    volatile char buf[65536];
    std::memset(const_cast<char *>(buf), 0xAA, sizeof(buf));
    g_churnSink += static_cast<std::uint64_t>(buf[4096]);
}

// ─── child: PreviewPanel destroyed mid-decode ──────────────────────────────
int childPreviewDestroy()
{
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    const QString dir = makeImageDir("preview", 1);
    const QString path = dir + "/img_00000.png";

    // Occupy the single Decode worker: the panel's preview task is ACCEPTED
    // but cannot run until after the panel is destroyed.
    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::milliseconds(1200)); });
    {
        PreviewPanel panel;
        panel.resize(320, 240);
        panel.setImage(path);
        // panel destroyed here — preview task still queued.
    }
    // The preview task now runs and its worker callback fires on a destroyed
    // panel (the destructor cancelled it, so the worker exits before decoding).
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until)
    {
        for (int i = 0; i < 32; ++i)
            churnStack(); // reuse the freed stack region while late callbacks fire
        pump(5);
    }
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    const auto m = sched.metrics(PoolType::DecodePool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 2;
    QDir qdir(dir);
    qdir.removeRecursively();
    printf("  child: preview destroy-mid-decode survived\n");
    return 0;
}

// ─── child: ImageViewer destroyed mid-decode ───────────────────────────────
int childViewerDestroy()
{
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);
    const QString dir = makeImageDir("viewer", 1);
    const QString path = dir + "/img_00000.png";

    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::milliseconds(1200)); });
    {
        ImageViewer viewer;
        viewer.resize(400, 300);
        viewer.setImage(path);
        // viewer destroyed here — decode still queued.
    }
    const auto until = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (std::chrono::steady_clock::now() < until)
    {
        for (int i = 0; i < 32; ++i)
            churnStack();
        pump(5);
    }
    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    const auto m = sched.metrics(PoolType::DecodePool);
    if (m.pending != 0 || m.active_tasks != 0)
        return 2;
    QDir qdir(dir);
    qdir.removeRecursively();
    printf("  child: viewer destroy-mid-decode survived\n");
    return 0;
}

// ─── parent: subprocess checks ─────────────────────────────────────────────
int runChild(QCoreApplication &app, const char *arg)
{
    QProcess proc;
    proc.setProcessChannelMode(QProcess::MergedChannels);
    proc.start(app.applicationFilePath(), QStringList{QString::fromLatin1(arg)});
    if (!proc.waitForStarted(10000))
        return -1;
    if (!proc.waitForFinished(60000))
    {
        proc.kill();
        proc.waitForFinished();
        return -2; // hung
    }
    if (proc.exitStatus() != QProcess::NormalExit)
        return -3; // crashed / terminated
    return proc.exitCode();
}

void testDestroyMidDecodeChildren(QCoreApplication &app)
{
    printf("\n[1. destroy-mid-decode: no callback into freed objects]\n");
    fflush(stdout);
    CHECK(runChild(app, "--child-preview-destroy") == 0,
          "PreviewPanel destroyed mid-decode survives");
    CHECK(runChild(app, "--child-viewer-destroy") == 0,
          "ImageViewer destroyed mid-decode survives");
}

// ─── 2. A -> B -> A navigation: newest generation only ─────────────────────
void testPreviewABA()
{
    printf("\n[2. PreviewPanel A->B->A: newest generation wins]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    const QString dir = makeImageDir("aba", 3);
    const QString a1 = dir + "/img_00000.png";
    const QString b = dir + "/img_00001.png";
    const QString a2 = dir + "/img_00000.png"; // same path as A1 on purpose

    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::milliseconds(800)); });

    PreviewPanel panel;
    panel.resize(320, 240);
    panel.setImage(a1); // queued (gen 1)
    panel.setImage(b);  // queued (gen 2)
    panel.setImage(a2); // queued (gen 3) — same path as the first request
    CHECK(!panel.hasImage(), "no preview while decodes are still queued");

    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(1000); // let the marshaled deliveries run
    CHECK(panel.hasImage(), "newest generation delivered");
    CHECK(panel.presentedPath() == a2 &&
              panel.presentationQuality() == PreviewPanel::PresentationQuality::Preview,
          "newest generation owns the presented path and upgraded quality");
    pump(1500);
    CHECK(panel.hasImage(), "no stale overwrite after settlement");
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0, "Thumbnail pool converges after A->B->A");
    CHECK(sched.graphMetrics().handles == 0, "no handle residue after A->B->A");

    QDir qdir(dir);
    qdir.removeRecursively();
}

// ─── 3. ImageViewer A -> B -> A via window title ───────────────────────────
void testViewerABA()
{
    printf("\n[3. ImageViewer A->B->A: newest generation wins]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    const QString dir = makeImageDir("vaba", 2);
    const QString a1 = dir + "/img_00000.png";
    const QString b = dir + "/img_00001.png";

    auto blocker = sched.submit(Priority::Decode, [](const TaskContext &)
                                { std::this_thread::sleep_for(std::chrono::milliseconds(800)); });

    ImageViewer viewer;
    viewer.resize(400, 300);
    viewer.setImage(a1); // queued (gen 1)
    viewer.setImage(b);  // queued (gen 2)
    viewer.setImage(a1); // queued (gen 3)

    sched.drain(PoolType::DecodePool, std::chrono::seconds(15));
    pump(1500);
    const QString title = viewer.windowTitle();
    CHECK(title.contains("img_00000"), "newest generation (A) delivered to the viewer");
    CHECK(!title.contains("无法加载"), "viewer shows a loaded image, not a failure");
    pump(1000);
    CHECK(viewer.windowTitle().contains("img_00000"), "no stale overwrite after settlement");

    QDir qdir(dir);
    qdir.removeRecursively();
}

// ─── 4. P5: rejected requests must reach a terminal UI state ───────────────
// A paused DecodePool rejects preview submissions; a paused DecodePool
// rejects viewer submissions. The panel/viewer must NOT sit in an eternal
// loading state: the panel shows no-image, the viewer shows the failure title.
void testRejectionReachesTerminalState()
{
    printf("\n[4. rejected requests reach a terminal UI state]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();

    sched.pause(PoolType::DecodePool);
    {
        PreviewPanel panel;
        panel.resize(320, 240);
        panel.setImage("C:/definitely/missing/preview.png");
        pump(300);
        CHECK(!panel.hasImage(),
              "rejected preview request leaves no-image state (no eternal loading)");
        CHECK(panel.sourceImageSize().isEmpty(),
              "rejected preview request exposes an empty source size");
        CHECK(panel.previewPixelSize().isEmpty(),
              "rejected preview request exposes an empty preview size");
    }
    sched.resume(PoolType::DecodePool);
    {
        sched.pause(PoolType::DecodePool);
        ImageViewer viewer;
        viewer.resize(400, 300);
        viewer.setImage("C:/definitely/missing/viewer.png");
        pump(300);
        CHECK(viewer.windowTitle().contains("无法加载"),
              "rejected viewer request reaches the failure title (terminal state)");
        sched.resume(PoolType::DecodePool);
    }
    const auto mt = sched.metrics(PoolType::DecodePool);
    CHECK(mt.pending == 0 && mt.active_tasks == 0,
          "Decode pool converges after rejected preview request");
    const auto m = sched.metrics(PoolType::DecodePool);
    CHECK(m.pending == 0 && m.active_tasks == 0,
          "Decode pool converges after rejected viewer request");
}

// ─── 5. P9: real 24MP UI completion latency ────────────────────────────────
// Measures the UI-thread event gap of the whole preview/viewer completion
// path (ImageData -> toQImage -> QPixmap::fromImage -> scaling/repaint),
// NOT just the worker-side stats. A genuine 24MP frame is decoded and the
// single event-loop call that delivers it is timed.
struct EventGapMeter
{
    qint64 worstCallNs = 0;
    qint64 totalNs = 0;
    int calls = 0;

    qint64 pumpUntil(const std::function<bool()> &done, int timeoutMs)
    {
        QElapsedTimer timer;
        const auto deadline =
            std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
        while (std::chrono::steady_clock::now() < deadline)
        {
            if (done())
                break;
            timer.start();
            QCoreApplication::processEvents(QEventLoop::AllEvents, 32);
            const qint64 callNs = timer.nsecsElapsed();
            totalNs += callNs;
            ++calls;
            if (callNs > worstCallNs)
                worstCallNs = callNs;
        }
        return worstCallNs;
    }
};

void testPreviewUILatency24MP()
{
    printf("\n[5. P9: 24MP preview completion UI event gap]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    const QString dir = QDir::tempPath() + "/mviewer_m27_24mp_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    const QString path = dir + "/big_24mp.png";
    {
        QImage big(6000, 4000, QImage::Format_RGB32);
        for (int y = 0; y < 4000; y += 40)
        {
            QRgb *row = reinterpret_cast<QRgb *>(big.scanLine(y));
            for (int x = 0; x < 6000; ++x)
                row[x] = qRgb((x * 255) / 6000, (y * 255) / 4000, ((x + y) * 255) / 10000);
        }
        big.save(path, "PNG");
    }

    auto &repo = ImageRepository::instance();
    const std::string key = repo.makeKey(path.toStdString());
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "24MP preview: Decode pool drained before measuring");
    CHECK(sched.drain(PoolType::DecodePool, std::chrono::seconds(15)),
          "24MP preview: Decode pool drained before measuring");
    const uint64_t decodeSubmittedBefore = sched.metrics(PoolType::DecodePool).submitted;
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, key, probe),
              "24MP preview: FullImage cache does not hold the key beforehand");
    }

    PreviewPanel panel;
    panel.resize(320, 240);
    panel.setImage(path);
    EventGapMeter meter;
    const qint64 worstNs = meter.pumpUntil([&] { return panel.hasImage(); }, 60000);
    const double worstMs = static_cast<double>(worstNs) / 1e6;
    const double avgMs = meter.calls ? static_cast<double>(meter.totalNs) / 1e6 / meter.calls : 0.0;
    printf("  24MP preview: worst event call %.1f ms, avg %.1f ms over %d calls\n", worstMs, avgMs,
           meter.calls);
    CHECK(panel.hasImage(), "24MP preview delivered");
    // The preview completion must not stall the UI thread for a quarter
    // second. The scaled decode runs on the Thumbnail worker; the UI thread
    // only materializes the <=512 QPixmap.
    CHECK(worstMs < 250.0, "24MP preview completion UI gap < 250 ms");

    CHECK(sched.metrics(PoolType::DecodePool).submitted == decodeSubmittedBefore + 1,
          "24MP preview: exactly one foreground Decode task serves the preview");
    {
        ImageData probe;
        CHECK(!CacheManager::instance().getMemory(CacheLevel::FullImage, key, probe),
              "24MP preview: FullImage cache is never touched");
        CHECK(CacheManager::instance().getMemory(
                  CacheLevel::Preview, PreviewPanel::previewCacheKey(path.toStdString()), probe),
              "24MP preview: scaled result lands in the Preview cache");
    }
    CHECK(panel.sourceImageSize() == QSize(6000, 4000),
          "24MP preview: source dimensions are preserved");
    const QSize previewSize = panel.previewPixelSize();
    CHECK(previewSize.width() > 0 && previewSize.height() > 0 &&
              std::max(previewSize.width(), previewSize.height()) <= 512,
          "24MP preview: preview pixmap is capped at a 512 max edge");

    QDir qdir(dir);
    qdir.removeRecursively();
}

void testViewerUILatency24MP()
{
    printf("\n[6. P9: 24MP viewer materialization UI event gap]\n");
    fflush(stdout);
    auto &sched = TaskScheduler::instance();
    sched.setQueueMaxThreads(Priority::Decode, 1);

    const QString dir = QDir::tempPath() + "/mviewer_m27_24mpv_" +
                        QString::number(QCoreApplication::applicationPid());
    QDir().mkpath(dir);
    const QString path = dir + "/big_24mp.png";
    {
        QImage big(6000, 4000, QImage::Format_RGB32);
        for (int y = 0; y < 4000; y += 40)
        {
            QRgb *row = reinterpret_cast<QRgb *>(big.scanLine(y));
            for (int x = 0; x < 6000; ++x)
                row[x] = qRgb((x * 255) / 6000, (y * 255) / 4000, ((x + y) * 255) / 10000);
        }
        big.save(path, "PNG");
    }

    ImageViewer viewer;
    viewer.resize(800, 600);
    viewer.setImage(path);
    EventGapMeter meter;
    const qint64 worstNs =
        meter.pumpUntil([&] { return viewer.windowTitle().contains("(6000x4000)"); }, 60000);
    const double worstMs = static_cast<double>(worstNs) / 1e6;
    printf("  24MP viewer: worst event call %.1f ms\n", worstMs);
    CHECK(viewer.windowTitle().contains("(6000x4000)"), "24MP viewer delivered");
    CHECK(worstMs < 500.0, "24MP viewer materialization UI gap < 500 ms");

    QDir qdir(dir);
    qdir.removeRecursively();
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    if (argc > 1)
    {
        const std::string mode = argv[1];
        if (mode == "--child-preview-destroy")
            return childPreviewDestroy();
        if (mode == "--child-viewer-destroy")
            return childViewerDestroy();
        fprintf(stderr, "unknown child mode: %s\n", mode.c_str());
        return 127;
    }

    printf("=== M27 QObject async lifetime tests ===\n");
    fflush(stdout);

    testDestroyMidDecodeChildren(app);
    testPreviewABA();
    testViewerABA();
    testRejectionReachesTerminalState();
    testPreviewUILatency24MP();
    testViewerUILatency24MP();

    auto &sched = TaskScheduler::instance();
    sched.resume(PoolType::DecodePool);
    sched.resume(PoolType::ThumbnailPool);
    sched.setPoolMaxThreads(PoolType::DecodePool, qMax(1, QThread::idealThreadCount()));
    sched.setPoolMaxThreads(PoolType::ThumbnailPool, qMax(1, QThread::idealThreadCount()));

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
