// M55 Phase-0 baseline recorder.
//
// This executable is intentionally not a pass/fail gate: it records the
// current machine's Browse working set and milestone timings so a later run
// can be compared with the same corpus and build. Native fullscreen feel and
// long-session visual smoothness remain explicit MANUAL/BLOCKED items.
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QScrollBar>
#include <QTemporaryDir>

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <psapi.h>
#endif

namespace
{

struct Metrics
{
    qint64 shellMs = -1;
    qint64 firstRowMs = -1;
    qint64 firstThumbMs = -1;
    qint64 screen50Ms = -1;
    qint64 screen95Ms = -1;
    qint64 scanCompleteMs = -1;
    qint64 stableMs = -1;
    qint64 jumpP50Ms = -1;
    qint64 jumpP95Ms = -1;
    qint64 workingSetStart = 0;
    qint64 workingSetPeak = 0;
    qint64 workingSetEnd = 0;
    qint64 privateBytesPeak = 0;
    size_t peakPending = 0;
    size_t peakHandles = 0;
    int readyPixmapPeak = 0;
    qint64 readyPixmapBytesPeak = 0;
};

void sampleProcess(Metrics &metrics, const ThumbnailPanel *panel = nullptr)
{
#ifdef _WIN32
    PROCESS_MEMORY_COUNTERS_EX counters{};
    counters.cb = sizeof(counters);
    if (GetProcessMemoryInfo(GetCurrentProcess(), reinterpret_cast<PROCESS_MEMORY_COUNTERS *>(
                                                               &counters), sizeof(counters)))
    {
        const qint64 working = static_cast<qint64>(counters.WorkingSetSize);
        const qint64 privateBytes = static_cast<qint64>(counters.PrivateUsage);
        if (metrics.workingSetStart == 0)
            metrics.workingSetStart = working;
        metrics.workingSetPeak = std::max(metrics.workingSetPeak, working);
        metrics.workingSetEnd = working;
        metrics.privateBytesPeak = std::max(metrics.privateBytesPeak, privateBytes);
    }
#else
    (void)metrics;
#endif
    for (int pool = 0; pool < 5; ++pool)
    {
        const auto m = TaskScheduler::instance().metrics(
            static_cast<TaskScheduler::PoolType>(pool));
        metrics.peakPending = std::max(metrics.peakPending, m.pending);
    }
    metrics.peakHandles =
        std::max(metrics.peakHandles, ThumbnailPipeline::instance().handlesCount());
    if (panel)
    {
        metrics.readyPixmapPeak =
            std::max(metrics.readyPixmapPeak, panel->thumbReadyCount());
        metrics.readyPixmapBytesPeak =
            std::max(metrics.readyPixmapBytesPeak, panel->thumbReadyBytes());
    }
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs, Metrics &metrics,
               const ThumbnailPanel *panel = nullptr)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 2);
        sampleProcess(metrics, panel);
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QApplication::processEvents(QEventLoop::AllEvents, 2);
    sampleProcess(metrics, panel);
    return predicate();
}

bool createCorpus(const QString &root, int count)
{
    std::printf("M55 corpus_start count=%d root=%s\n", count, root.toUtf8().constData());
    std::fflush(stdout);
    if (!QDir().mkpath(root))
        return false;
    QImage seed(2, 2, QImage::Format_RGB32);
    seed.fill(qRgb(37, 89, 143));
    const QString seedPath = QDir(root).filePath(QStringLiteral("seed.png"));
    if (!seed.save(seedPath, "PNG"))
        return false;
    for (int i = 0; i < count; ++i)
    {
        const QString path = QDir(root).filePath(
            QStringLiteral("img_%1.png").arg(i, 6, 10, QLatin1Char('0')));
        if (!QFile::copy(seedPath, path))
            return false;
    }
    QFile::remove(seedPath);
    std::printf("M55 corpus_ready count=%d\n", count);
    std::fflush(stdout);
    return true;
}

int screenCount(const ThumbnailPanel &panel)
{
    const int cellW = std::max(1, panel.gridSize().width());
    const int cellH = std::max(1, panel.gridSize().height());
    const int cols = std::max(1, panel.viewport()->width() / cellW);
    const int rows = std::max(1, panel.viewport()->height() / cellH + 1);
    return cols * rows;
}

int readyCount(const ThumbnailPanel &panel, const QStringList &paths)
{
    int ready = 0;
    for (const QString &path : paths)
    {
        if (!panel.thumbReady(path).isNull())
            ++ready;
    }
    return ready;
}

Metrics record(const QString &directory, int expected)
{
    Metrics metrics;
    std::printf("M55 record_start expected=%d\n", expected);
    std::fflush(stdout);
    ThumbnailPipeline::instance().clear();
    ThumbnailPanel panel;
    panel.resize(900, 620);
    panel.show();
    QApplication::processEvents(QEventLoop::AllEvents, 5);
    sampleProcess(metrics, &panel);

    QElapsedTimer timer;
    timer.start();
    panel.setDirectory(directory);
    metrics.shellMs = timer.elapsed();
    std::printf("M55 directory_requested expected=%d shell=%lld\n", expected,
                static_cast<long long>(metrics.shellMs));
    std::fflush(stdout);
    waitUntil([&] { return !panel.pathList().isEmpty(); }, 60000, metrics, &panel);
    metrics.firstRowMs = timer.elapsed();

    const QStringList screenPaths = panel.pathList().first(screenCount(panel));
    waitUntil([&] { return readyCount(panel, screenPaths) >= 1; }, 60000, metrics, &panel);
    metrics.firstThumbMs = timer.elapsed();
    waitUntil([&] { return readyCount(panel, screenPaths) * 2 >= screenPaths.size(); }, 60000,
              metrics, &panel);
    metrics.screen50Ms = timer.elapsed();
    waitUntil([&] { return readyCount(panel, screenPaths) * 20 >= screenPaths.size() * 19; },
              60000, metrics, &panel);
    metrics.screen95Ms = timer.elapsed();
    waitUntil([&] { return panel.pathList().size() == expected; }, 120000, metrics, &panel);
    metrics.scanCompleteMs = timer.elapsed();

    QScrollBar *bar = panel.verticalScrollBar();
    std::vector<qint64> jumps;
    if (bar && bar->maximum() > 0 && panel.pathList().size() == expected)
    {
        for (int i = 1; i <= 8; ++i)
        {
            const int value = bar->maximum() * i / 8;
            QElapsedTimer jump;
            jump.start();
            bar->setValue(value);
            QApplication::processEvents(QEventLoop::AllEvents, 2);
            const QModelIndex visible = panel.indexAt(QPoint(2, 2));
            const QString targetPath = visible.isValid()
                                            ? panel.pathList().value(visible.row())
                                            : panel.pathList().value(std::min(expected - 1,
                                                                              expected * i / 8));
            waitUntil([&] { return !panel.thumbReady(targetPath).isNull(); }, 30000, metrics,
                      &panel);
            jumps.push_back(jump.elapsed());
        }
    }
    if (!jumps.empty())
    {
        std::sort(jumps.begin(), jumps.end());
        metrics.jumpP50Ms = jumps[jumps.size() / 2];
        metrics.jumpP95Ms = jumps[(jumps.size() * 95 + 99) / 100 - 1];
    }
    waitUntil(
        []
        {
            return ThumbnailPipeline::instance().pendingCount() == 0 &&
                   ThumbnailPipeline::instance().handlesCount() == 0;
        },
        60000, metrics, &panel);
    metrics.stableMs = timer.elapsed();
    sampleProcess(metrics, &panel);
    return metrics;
}

void printMetrics(const char *label, const Metrics &m)
{
    std::printf(
        "M55 %s shell=%lld first_row=%lld first_thumb=%lld screen50=%lld screen95=%lld "
        "scan_complete=%lld stable=%lld jump_p50=%lld jump_p95=%lld "
        "working_set_start=%lld working_set_peak=%lld working_set_end=%lld "
        "private_peak=%lld peak_pending=%zu peak_handles=%zu ready_pixmap_peak=%d "
        "ready_pixmap_bytes_peak=%lld\n",
        label, static_cast<long long>(m.shellMs), static_cast<long long>(m.firstRowMs),
        static_cast<long long>(m.firstThumbMs), static_cast<long long>(m.screen50Ms),
        static_cast<long long>(m.screen95Ms), static_cast<long long>(m.scanCompleteMs),
        static_cast<long long>(m.stableMs), static_cast<long long>(m.jumpP50Ms),
        static_cast<long long>(m.jumpP95Ms), static_cast<long long>(m.workingSetStart),
        static_cast<long long>(m.workingSetPeak), static_cast<long long>(m.workingSetEnd),
        static_cast<long long>(m.privateBytesPeak), m.peakPending, m.peakHandles,
        m.readyPixmapPeak, static_cast<long long>(m.readyPixmapBytesPeak));
}

} // namespace

int main(int argc, char **argv)
{
    std::printf("M55 main_start\n");
    std::fflush(stdout);
    QApplication app(argc, argv);
    std::printf("M55 app_ready\n");
    std::fflush(stdout);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 2;
    bool run10k = true;
    bool run50k = true;
    for (int i = 1; i < argc; ++i)
    {
        const QString arg = QString::fromLocal8Bit(argv[i]);
        if (arg == QStringLiteral("--10k"))
            run50k = false;
        else if (arg == QStringLiteral("--50k"))
            run10k = false;
    }
    const QString dir10k = QDir(temp.path()).filePath(QStringLiteral("images_10000"));
    const QString dir50k = QDir(temp.path()).filePath(QStringLiteral("images_50000"));
    if ((run10k && !createCorpus(dir10k, 10000)) ||
        (run50k && !createCorpus(dir50k, 50000)))
        return 2;

    std::printf("M55 interactive baseline recorder (milliseconds; native memory bytes)\n");
    std::fflush(stdout);
    if (run10k)
        printMetrics("10000", record(dir10k, 10000));
    if (run50k)
        printMetrics("50000", record(dir50k, 50000));
    std::printf("M55 fullscreen_sequential=MANUAL/BLOCKED (requires native GUI session)\n");
    std::printf("M55 100MP_display_raster=MANUAL/BLOCKED (use m47_viewer_lod_tests)\n");
    return 0;
}
