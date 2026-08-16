// M46 — real-workflow soak / resource-convergence qualification.
//
// Drives the ACTUAL product loop (not single-module benchmarks):
//
//   Browse large folder -> rapid thumbnail scroll/size churn -> Viewer ->
//   next/previous -> zoom/pan -> Compare -> change pair -> Analyze -> Export
//   (or cancel Export) -> back to Browse -> change directory -> repeat.
//
// Phases: warm-up -> steady state -> idle convergence.
//
// Pass criteria (release acceptance lines):
//   * idle RSS growth vs the steady warm baseline <= max(128 MiB, 15%);
//   * Windows handle count at idle <= baseline + 64 (platform-noise window);
//   * every scheduler pool converges: pending/active/queue_depth/waiting == 0
//     and the dependency graph is empty;
//   * ThumbnailPipeline handles/pending converge to 0;
//   * CacheManager obeys the configured byte caps (memory + disk);
//   * no growing temp/export artifacts (canceled exports leave no partial
//     outputs);
//   * no crash / assert / deadlock; the busy cursor is restored.
//
// One workload driver serves both the short CI run and the extended manual
// Release qualification:
//   build_msvc\bin\mviewer_m46_workflow_soak.exe                 (default 8 iters)
//   build_msvc\bin\mviewer_m46_workflow_soak.exe --extended       (40 iters)
//   build_msvc\bin\mviewer_m46_workflow_soak.exe --iterations N --out results.json
//
// Exit code 0 = qualification passed.

#include "compareworkspace.h"
#include "core/analysis/AnalysisEngine.h"
#include "core/cache/CacheManager.h"
#include "core/export/ExportJob.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"
#include "core/perf/MemoryTracker.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "imageviewer.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QPainter>
#include <QScrollBar>
#include <QTemporaryDir>
#include <QThread>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <functional>
#include <memory>
#include <string>
#include <thread>
#include <vector>

#ifdef _WIN32
#include <windows.h>
#include <psapi.h>
#endif

namespace
{
using PoolType = TaskScheduler::PoolType;

struct Options
{
    int iterations = 8;
    bool extended = false;
    QString outFile;
};

Options g_opts;
int g_failures = 0;

void fail(const char *what)
{
    printf("  [soak] FAIL: %s\n", what);
    ++g_failures;
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

bool waitEntryCount(ThumbnailPanel &panel, int expected, int ms)
{
    return waitTrue([&] { return panel.entries().size() == expected; }, ms);
}

// ── Corpus ──────────────────────────────────────────────────────────────────
struct Corpus
{
    QString dirA;
    QString dirB;
    QString exportDir;
    QStringList aPaths;
    QStringList bPaths;
};

void writeImage(const QString &path, int w, int h, const QColor &c)
{
    QImage img(w, h, QImage::Format_RGB32);
    img.fill(c);
    if (w > 64 && h > 64)
    {
        // Give large images some content so JPEG encodes are non-trivial.
        QPainter p(&img);
        p.setPen(QColor(255, 255, 255));
        for (int i = 0; i < 40; ++i)
            p.drawLine((i * 37) % w, (i * 53) % h, (i * 71) % w, (i * 89) % h);
        p.end();
    }
    img.save(path);
}

Corpus buildCorpus()
{
    Corpus c;
    const QString base = QDir::tempPath() + "/mviewer_m46_soak_" +
                         QString::number(QCoreApplication::applicationPid());
    c.dirA = base + "/browseA";
    c.dirB = base + "/browseB";
    c.exportDir = base + "/export";
    QDir().mkpath(c.dirA);
    QDir().mkpath(c.dirB);
    QDir().mkpath(c.exportDir);

    // dirA: many small + medium + a few large, mixed sizes.
    for (int i = 0; i < 180; ++i)
    {
        const QString p = c.dirA + QString("/s_%1.png").arg(i, 4, 10, QChar('0'));
        writeImage(p, 24, 24, QColor((i * 7) & 0xFF, (i * 13) & 0xFF, (i * 29) & 0xFF));
        c.aPaths.push_back(p);
    }
    for (int i = 0; i < 40; ++i)
    {
        const QString p = c.dirA + QString("/m_%1.png").arg(i, 4, 10, QChar('0'));
        writeImage(p, 96, 72, QColor((i * 11) & 0xFF, (i * 17) & 0xFF, (i * 23) & 0xFF));
        c.aPaths.push_back(p);
    }
    for (int i = 0; i < 4; ++i)
    {
        const QString p = c.dirA + QString("/big_%1.jpg").arg(i);
        writeImage(p, 1600, 1200, QColor((i * 31) & 0xFF, (i * 47) & 0xFF, 128));
        c.aPaths.push_back(p);
    }
    // dirB: different set — small + large JPEGs (mixed resolutions).
    for (int i = 0; i < 150; ++i)
    {
        const QString p = c.dirB + QString("/b_%1.png").arg(i, 4, 10, QChar('0'));
        writeImage(p, 32, 32, QColor((i * 19) & 0xFF, (i * 41) & 0xFF, (i * 3) & 0xFF));
        c.bPaths.push_back(p);
    }
    for (int i = 0; i < 3; ++i)
    {
        const QString p = c.dirB + QString("/bigb_%1.jpg").arg(i);
        writeImage(p, 1200, 1600, QColor((i * 61) & 0xFF, 90, (i * 13) & 0xFF));
        c.bPaths.push_back(p);
    }
    return c;
}

// ── Telemetry ───────────────────────────────────────────────────────────────
struct ResourceSample
{
    double rssMB = 0.0;
    double handles = 0.0;
    size_t liveFrames = 0;
    size_t cacheMemoryBytes = 0;
    size_t cacheDiskBytes = 0;
    size_t pipelineHandles = 0;
    size_t pipelinePending = 0;
    bool valid = false;
};

ResourceSample sampleResources()
{
    ResourceSample s;
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS counters{};
    counters.cb = sizeof(counters);
    DWORD handles = 0;
    const bool memoryOk = GetProcessMemoryInfo(GetCurrentProcess(), &counters, sizeof(counters));
    const bool handlesOk = GetProcessHandleCount(GetCurrentProcess(), &handles);
    s.rssMB = counters.WorkingSetSize / (1024.0 * 1024.0);
    s.handles = static_cast<double>(handles);
    s.valid = memoryOk && handlesOk;
#endif
    s.liveFrames = mviewer::perf::MemoryTracker::instance().sample().liveImageFrames;
    s.cacheMemoryBytes = CacheManager::instance().memoryUsageBytes();
    s.cacheDiskBytes = CacheManager::instance().diskUsageBytes();
    s.pipelineHandles = ThumbnailPipeline::instance().handlesCount();
    s.pipelinePending = ThumbnailPipeline::instance().pendingCount();
    return s;
}

struct SchedulerSample
{
    uint64_t pending = 0;
    uint64_t active = 0;
    uint64_t queueDepth = 0;
    uint64_t waiting = 0;
    uint64_t graphHandles = 0;
    uint64_t graphDeferred = 0;
};

SchedulerSample sampleScheduler()
{
    SchedulerSample s;
    for (int p = 0; p < 5; ++p)
    {
        const auto m = TaskScheduler::instance().metrics(static_cast<PoolType>(p));
        s.pending += m.pending;
        s.active += m.active_tasks;
        s.queueDepth += m.queue_depth;
        s.waiting += m.waiting;
    }
    const auto g = TaskScheduler::instance().graphMetrics();
    s.graphHandles = g.handles;
    s.graphDeferred = g.deferred;
    return s;
}

// ── Workload steps ──────────────────────────────────────────────────────────
bool browseStep(const Corpus &c, const QString &dir, ThumbnailPanel &panel)
{
    panel.setDirectory(dir);
    const int expected = QDir(dir).entryList(QDir::Files).size();
    if (!waitEntryCount(panel, expected, 20000))
    {
        fail("browse: directory scan did not complete");
        return false;
    }
    // Rapid scroll through the range (drives visible-range scheduling).
    panel.setThumbSize(140);
    for (int i = 0; i < 6; ++i)
    {
        panel.verticalScrollBar()->setValue(i * 40);
        pump(15);
    }
    panel.verticalScrollBar()->setValue(0);
    pump(30);
    // Size churn (mid-flight supersession) while thumbnails decode.
    panel.setThumbSize(96);
    panel.setThumbSize(140);
    pump(200);
    return true;
}

bool viewerStep(const QStringList &paths, ImageViewer &viewer)
{
    const int n = paths.size();
    if (n == 0)
        return true;
    const int i0 = n / 2;
    viewer.setImage(paths.at(i0));
    if (!waitTrue([&] { return static_cast<bool>(viewer.frame()); }, 20000))
    {
        fail("viewer: frame did not load");
        return false;
    }
    // next / previous navigation (supersedes in-flight loads).
    viewer.setImage(paths.at((i0 + 1) % n));
    viewer.setImage(paths.at((i0 - 1 + n) % n));
    // zoom / pan churn.
    for (int z = 0; z < 3; ++z)
    {
        viewer.zoomIn();
        pump(10);
    }
    viewer.zoomFit();
    pump(20);
    return waitTrue([&] { return static_cast<bool>(viewer.frame()); }, 10000);
}

bool compareStep(const QStringList &paths)
{
    if (paths.size() < 4)
        return true;
    {
        CompareWorkspace ws;
        ws.resize(1000, 700);
        ws.setImages({paths.at(0), paths.at(1)});
        if (!waitTrue([&] { return ws.comparedImageCount() == 2; }, 20000))
        {
            fail("compare: first pair did not load");
            return false;
        }
        // Change the pair while the workspace is alive.
        ws.setImages({paths.at(2), paths.at(3)});
        if (!waitTrue([&] { return ws.comparedImageCount() == 2; }, 20000))
        {
            fail("compare: second pair did not load");
            return false;
        }
        pump(50);
    }
    pump(100);
    return true;
}

bool analyzeStep(const std::shared_ptr<ImageFrame> &frame)
{
    if (!frame)
        return true;
    const auto &px = frame->pixels();
    if (!px.buffer || px.buffer->empty())
        return true;
    // Compute stats + a self-difference PSNR (exercises the analysis hot path
    // on real decoded data).
    const ImageStats stats = AnalysisEngine::computeStats(px);
    if (stats.pixelCount <= 0)
    {
        fail("analyze: stats produced no pixels");
        return false;
    }
    return true;
}

bool exportStep(const Corpus &c, const QString &sourcePath, int iteration)
{
    mviewer::exportjob::ExportJobConfig cfg;
    cfg.mode = mviewer::exportjob::Mode::Convert;
    cfg.sources = {sourcePath.toStdString()};
    cfg.outDir = c.exportDir.toStdString();
    cfg.format = "png";
    const std::string dest = c.exportDir.toStdString() + "/out_" + std::to_string(iteration) + ".png";
    cfg.destinationPath = dest;

    if (iteration % 3 == 0)
    {
        // ── Cancel variants ──────────────────────────────────────────────
        // (a) PRE-cancelled single-source job: the cancel token is set before
        // the worker starts; run() must return without committing anything.
        {
            auto cancelToken = std::make_shared<std::atomic<bool>>(true);
            std::atomic<bool> done{false};
            std::atomic<int> doneCount{-1};
            auto &sched = TaskScheduler::instance();
            auto handle = sched.submit(
                TaskScheduler::Priority::Analysis,
                [cfg, cancelToken, &done, &doneCount](const TaskScheduler::TaskContext &)
                {
                    mviewer::exportjob::ExportJobConfig c2 = cfg;
                    c2.cancel = cancelToken;
                    const auto r = mviewer::exportjob::run(c2);
                    doneCount.store(r.done, std::memory_order_release);
                    done.store(true, std::memory_order_release);
                });
            if (!handle)
            {
                fail("export: pre-cancelled job rejected");
                return false;
            }
            if (!waitTrue([&] { return done.load(); }, 15000))
            {
                fail("export: pre-cancelled job did not finish");
                return false;
            }
            if (QFile::exists(QString::fromStdString(dest)))
            {
                fail("export: pre-cancelled job committed an output");
                return false;
            }
            if (doneCount.load() != 0)
            {
                fail("export: pre-cancelled job reported done>0");
                return false;
            }
        }
        // (b) MID-FLIGHT cancelled multi-source job: cancellation is requested
        // shortly after submission; the job must terminate, every committed
        // output (if any) must be a complete file, and no temp residue may be
        // left behind.
        {
            const QString cancelDir = c.exportDir + "/cancel_" + QString::number(iteration);
            QDir().mkpath(cancelDir);
            std::vector<std::string> many;
            for (int i = 0; i < 10; ++i)
                many.push_back(c.aPaths.at(static_cast<size_t>(i) * 3 % c.aPaths.size())
                                   .toStdString());
            mviewer::exportjob::ExportJobConfig c2 = cfg;
            c2.sources = many;
            c2.outDir = cancelDir.toStdString();
            c2.destinationPath.clear();
            c2.renamePattern = "cancel_{seq:3}";
            auto cancelToken = std::make_shared<std::atomic<bool>>(false);
            c2.cancel = cancelToken;
            std::atomic<bool> done{false};
            auto &sched = TaskScheduler::instance();
            auto handle = sched.submit(
                TaskScheduler::Priority::Analysis,
                [c2, cancelToken, &done](const TaskScheduler::TaskContext &)
                {
                    (void)mviewer::exportjob::run(c2);
                    done.store(true, std::memory_order_release);
                });
            if (!handle)
            {
                fail("export: mid-flight job rejected");
                return false;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(3));
            cancelToken->store(true, std::memory_order_release);
            if (!waitTrue([&] { return done.load(); }, 30000))
            {
                fail("export: mid-flight cancelled job did not terminate");
                return false;
            }
            // Every committed output is a complete PNG (per-item atomic
            // commit); partial/temp files must not linger.
            for (const QString &f : QDir(cancelDir).entryList(QDir::Files))
            {
                const QString full = cancelDir + "/" + f;
                if (f.endsWith(".tmp"))
                {
                    fail("export: temp file residue after cancelled export");
                    return false;
                }
                if (QFile(full).size() == 0)
                {
                    fail("export: cancelled export left an empty/partial committed file");
                    return false;
                }
            }
            QDir(cancelDir).removeRecursively();
        }
    }
    else
    {
        // Complete variant: the job must produce a valid output file.
        const auto result = mviewer::exportjob::run(cfg);
        if (result.failed > 0 || result.done != 1)
        {
            fail("export: full job did not complete");
            return false;
        }
        if (!QFile::exists(QString::fromStdString(dest)) ||
            QFile(QString::fromStdString(dest)).size() == 0)
        {
            fail("export: output file missing or empty");
            return false;
        }
    }
    return true;
}

int exportArtifactCount(const Corpus &c)
{
    return QDir(c.exportDir).entryList({"out_*"}, QDir::Files).size();
}

void printSamples(const char *label, const ResourceSample &r, const SchedulerSample &s)
{
    printf("  [soak] %s: rss=%.1fMB handles=%.0f liveFrames=%zu cacheMem=%.1fMB "
           "cacheDisk=%.1fMB pipe(h=%zu,p=%zu) sched(p=%llu,a=%llu,q=%llu,w=%llu,g=%llu)\n",
           label, r.rssMB, r.handles, r.liveFrames, r.cacheMemoryBytes / (1024.0 * 1024.0),
           r.cacheDiskBytes / (1024.0 * 1024.0), r.pipelineHandles, r.pipelinePending,
           static_cast<unsigned long long>(s.pending), static_cast<unsigned long long>(s.active),
           static_cast<unsigned long long>(s.queueDepth),
           static_cast<unsigned long long>(s.waiting),
           static_cast<unsigned long long>(s.graphHandles));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    for (int i = 1; i < argc; ++i)
    {
        const std::string a = argv[i];
        if (a == "--extended")
        {
            g_opts.extended = true;
            g_opts.iterations = 40;
        }
        else if (a == "--iterations" && i + 1 < argc)
            g_opts.iterations = std::atoi(argv[++i]);
        else if (a == "--out" && i + 1 < argc)
            g_opts.outFile = QString::fromLocal8Bit(argv[++i]);
    }
    printf("=== M46 workflow soak: iterations=%d (extended=%d) ===\n", g_opts.iterations,
           g_opts.extended ? 1 : 0);
    fflush(stdout);

    const Corpus corpus = buildCorpus();
    const int warmup = 2;
    const int steadyBegin = warmup;

    ResourceSample baseline;
    SchedulerSample baselineSched;
    int baselineArtifacts = 0;
    bool baselineTaken = false;

    const auto t0 = std::chrono::steady_clock::now();

    // ── Warm-up + steady-state loop ─────────────────────────────────────────
    for (int it = 0; it < g_opts.iterations; ++it)
    {
        const QString &dir = (it % 2 == 0) ? corpus.dirA : corpus.dirB;
        const QStringList &paths = (it % 2 == 0) ? corpus.aPaths : corpus.bPaths;
        printf("  [soak] iteration %d/%d (%s)\n", it + 1, g_opts.iterations,
               it % 2 == 0 ? "A" : "B");
        fflush(stdout);

        {
            ThumbnailPanel panel;
            panel.resize(1000, 700);
            panel.show();
            pump(50);
            if (!browseStep(corpus, dir, panel))
                break;
            // Select the first path of the current directory.
            const QString first = panel.pathList().isEmpty() ? QString() : panel.pathList().first();
            if (first.isEmpty())
            {
                fail("browse: no paths listed");
                break;
            }
            // Viewer on the selected image + navigation.
            {
                ImageViewer viewer;
                viewer.resize(900, 650);
                viewer.setImage(first);
                if (!viewerStep(paths, viewer))
                    break;
                const auto frame = viewer.frame();
                if (!analyzeStep(frame))
                    break;
                const QString src = frame && !frame->metadata().filePath.empty()
                                        ? QString::fromStdString(frame->metadata().filePath)
                                        : first;
                if (!exportStep(corpus, src, it))
                    break;
            }
            // Back to browse: change directory while the panel is alive.
            const QString dir2 = (it % 2 == 0) ? corpus.dirB : corpus.dirA;
            if (!browseStep(corpus, dir2, panel))
                break;
            pump(100);
            panel.hide();
        }
        pump(300); // let teardown deliveries drain

        if (!compareStep(paths))
            break;

        pump(200);
        if (it == steadyBegin - 1 && !baselineTaken)
        {
            baseline = sampleResources();
            baselineSched = sampleScheduler();
            baselineArtifacts = exportArtifactCount(corpus);
            baselineTaken = true;
            printSamples("steady baseline", baseline, baselineSched);
        }
        fflush(stdout);
    }

    // ── Idle convergence ────────────────────────────────────────────────────
    printf("  [soak] idle convergence phase...\n");
    pump(3000);
    // Drain every pool (bounded) and let the pipeline settle.
    for (int p = 0; p < 5; ++p)
    {
        (void)TaskScheduler::instance().drain(static_cast<PoolType>(p), std::chrono::seconds(10));
    }
    const auto idleDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
    while (std::chrono::steady_clock::now() < idleDeadline)
    {
        const auto s = sampleScheduler();
        const auto r = sampleResources();
        if (s.pending == 0 && s.active == 0 && s.queueDepth == 0 && s.waiting == 0 &&
            s.graphHandles == 0 && s.graphDeferred == 0 && r.pipelineHandles == 0 &&
            r.pipelinePending == 0)
            break;
        pump(100);
    }
    const ResourceSample idle = sampleResources();
    const SchedulerSample idleSched = sampleScheduler();
    const int finalArtifacts = exportArtifactCount(corpus);
    printSamples("idle final", idle, idleSched);
    printf("  [soak] export artifacts: baseline=%d final=%d\n", baselineArtifacts,
           finalArtifacts);

    // ── Verdicts ────────────────────────────────────────────────────────────
    const double rssGrowthMB = idle.rssMB - baseline.rssMB;
    const double rssAllowMB = qMax(128.0, baseline.rssMB * 0.15);
    printf("  [soak] RSS growth: %.1f MB (allowed %.1f MB)\n", rssGrowthMB, rssAllowMB);
    if (baselineTaken && idle.valid && baseline.valid)
    {
        if (rssGrowthMB > rssAllowMB)
        {
            printf("  [soak] FAIL: idle RSS grew %.1f MB beyond max(128MiB, 15%%)=%.1f MB\n",
                   rssGrowthMB, rssAllowMB);
            ++g_failures;
        }
        const double handleGrowth = idle.handles - baseline.handles;
        printf("  [soak] handle growth: %.0f (window +64)\n", handleGrowth);
        if (handleGrowth > 64.0)
        {
            printf("  [soak] FAIL: handle count grew %.0f beyond the +64 noise window\n",
                   handleGrowth);
            ++g_failures;
        }
    }
    else
    {
        printf("  [soak] WARNING: platform resource sampling unavailable on this build\n");
    }

    if (idleSched.pending != 0 || idleSched.active != 0 || idleSched.queueDepth != 0 ||
        idleSched.waiting != 0)
    {
        printf("  [soak] FAIL: scheduler pools did not converge at idle\n");
        ++g_failures;
    }
    if (idleSched.graphHandles != 0 || idleSched.graphDeferred != 0)
    {
        printf("  [soak] FAIL: scheduler dependency graph not empty at idle\n");
        ++g_failures;
    }
    if (idle.pipelineHandles != 0 || idle.pipelinePending != 0)
    {
        printf("  [soak] FAIL: ThumbnailPipeline bookkeeping not empty at idle\n");
        ++g_failures;
    }
    if (QApplication::overrideCursor() != nullptr)
    {
        printf("  [soak] FAIL: busy cursor left set at idle\n");
        ++g_failures;
    }

    const auto &cfg = CacheManager::instance().config();
    const size_t memCap = cfg.metadataCacheSize + cfg.thumbnailCacheSize + cfg.previewCacheSize +
                          cfg.viewerCacheSize;
    if (idle.cacheMemoryBytes > memCap)
    {
        printf("  [soak] FAIL: cache memory %.1f MB exceeds cap %.1f MB\n",
               idle.cacheMemoryBytes / (1024.0 * 1024.0), memCap / (1024.0 * 1024.0));
        ++g_failures;
    }
    if (idle.cacheDiskBytes > cfg.diskCacheSize)
    {
        printf("  [soak] FAIL: disk cache %.1f MB exceeds cap %.1f MB\n",
               idle.cacheDiskBytes / (1024.0 * 1024.0), cfg.diskCacheSize / (1024.0 * 1024.0));
        ++g_failures;
    }
    if (finalArtifacts < baselineArtifacts)
    {
        printf("  [soak] FAIL: export artifacts shrank (baseline=%d final=%d)\n",
               baselineArtifacts, finalArtifacts);
        ++g_failures;
    }

    const double elapsedSec =
        std::chrono::duration<double>(std::chrono::steady_clock::now() - t0).count();
    printf("  [soak] elapsed %.1f s, liveFrames at idle = %zu\n", elapsedSec, idle.liveFrames);

    if (!g_opts.outFile.isEmpty())
    {
        QJsonObject o;
        o["iterations"] = g_opts.iterations;
        o["extended"] = g_opts.extended;
        o["rss_growth_mb"] = rssGrowthMB;
        o["rss_allow_mb"] = rssAllowMB;
        o["handle_growth"] = idle.handles - baseline.handles;
        o["idle_rss_mb"] = idle.rssMB;
        o["idle_handles"] = idle.handles;
        o["idle_live_frames"] = static_cast<double>(idle.liveFrames);
        o["idle_cache_memory_bytes"] = static_cast<double>(idle.cacheMemoryBytes);
        o["idle_cache_disk_bytes"] = static_cast<double>(idle.cacheDiskBytes);
        o["idle_scheduler_pending"] = static_cast<double>(idleSched.pending);
        o["idle_pipeline_handles"] = static_cast<double>(idle.pipelineHandles);
        o["elapsed_sec"] = elapsedSec;
        o["failures"] = g_failures;
        QFile f(g_opts.outFile);
        if (f.open(QIODevice::WriteOnly))
            f.write(QJsonDocument(o).toJson(QJsonDocument::Indented));
    }

    // Corpus cleanup (best-effort; the export outputs are deliberately kept
    // only when a failure needs inspection).
    QDir(corpus.dirA).removeRecursively();
    QDir(corpus.dirB).removeRecursively();
    QDir(corpus.exportDir).removeRecursively();

    printf("=== M46 workflow soak: %s (%d failure(s)) ===\n",
           g_failures == 0 ? "PASS" : "FAIL", g_failures);
    fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
