#include "benchmark/scenarios.h"

#include "core/cache/CacheManager.h"
#include "core/command/CommandRegistry.h"
#include "core/image/ImageRepository.h"
#include "core/scheduler/TaskScheduler.h"
#include "imagelistmodel.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadatapanel.h"
#include "selectionmodel.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
#include <QTimer>
#include <QWidget>

#include <algorithm>
#include <chrono>
#include <functional>
#include <memory>
#include <sstream>

// B18/B19 — end-to-end view-switch latency through the real UI path.
//
// B7/B8 time ImageRepository::load() cache hits (~0.2 ms) and never touch the
// viewer. These scenarios issue the same next-image command the product uses
// (CommandRegistry "navigate_next" → MainWindow::navigate → SelectionModel →
// onCurrentImageChanged → ImageViewer::setImage plus preview/metadata/overlay
// updates) and wait until the viewer displays the new source AND a paint has
// completed (repaint + QWidget::render into a QImage).
//
// Report-only unless performance_budget.json puts the metric in scenario_map.
// Timeouts are counted in `detail` rather than hanging. `passed` stays true
// here; the harness applies a budget only when one exists. Waits are condition
// polls with a cap — they return as soon as the viewer shows the new image.

namespace mviewer::bench
{

namespace
{

constexpr int kSetupTimeoutMs = 8000;
constexpr int kPoolIdleCapMs = 250;
// Lower-is-better sentinel so a gated run fails closed when nothing was measured.
constexpr double kUnmeasuredMs = 1.0e9;

void pump()
{
    // Non-blocking turns only. processEvents(flags, ms) keeps spinning until
    // the deadline while a chatty UI keeps posting events, which turns a
    // "slice" into a fixed sleep.
    for (int i = 0; i < 4; ++i)
        QCoreApplication::processEvents(QEventLoop::AllEvents);
}

bool waitUntil(const std::function<bool()> &ready, int timeoutMs)
{
    if (timeoutMs < 1)
        timeoutMs = 1;
    if (ready())
        return true;
    // Wall clock, checked before ready(). If ready() blocks on this thread the
    // QTimer cannot fire, so the next poll still quits once the deadline passed.
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(timeoutMs);
    QEventLoop loop;
    QTimer timeout;
    timeout.setSingleShot(true);
    timeout.setTimerType(Qt::PreciseTimer);
    QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
    QTimer poll;
    poll.setTimerType(Qt::PreciseTimer);
    poll.setInterval(1);
    QObject::connect(&poll, &QTimer::timeout, &loop,
                     [&]
                     {
                         if (std::chrono::steady_clock::now() >= deadline)
                         {
                             loop.quit();
                             return;
                         }
                         if (ready())
                             loop.quit();
                     });
    timeout.start(timeoutMs);
    poll.start();
    loop.exec(QEventLoop::AllEvents);
    return ready();
}

void forcePaint(QWidget *widget)
{
    if (!widget)
        return;
    widget->repaint();
    if (widget->width() <= 0 || widget->height() <= 0)
        return;
    QImage buf(widget->size(), QImage::Format_ARGB32);
    if (!buf.isNull())
        widget->render(&buf);
}

ImageViewer *findViewer()
{
    for (QWidget *top : QApplication::topLevelWidgets())
    {
        if (auto *viewer = qobject_cast<ImageViewer *>(top))
            return viewer;
    }
    return nullptr;
}

bool pathEq(const QString &a, const QString &b)
{
    return ImageViewer::browsePathEquals(a, b);
}

bool viewerShows(ImageViewer *viewer, const QString &expected)
{
    if (!viewer || expected.isEmpty() || !pathEq(viewer->currentPath(), expected))
        return false;
    if (viewer->isLodDisplay() && !viewer->displayRaster().isNull())
        return true;
    const auto frame = viewer->frame();
    return frame && frame->isValid();
}

bool decodePoolIdle()
{
    const auto metrics = TaskScheduler::instance().metrics(TaskScheduler::DecodePool);
    return metrics.pending == 0 && metrics.active_tasks == 0;
}

void evictCaches()
{
    // Do not call TaskScheduler::drain() here. Its success path joins and
    // destroys the pool, and a pool that never goes idle blocks for the whole
    // timeout (previously 2000 ms, twice per cold sample).
    ImageRepository::instance().invalidateAll();
    CacheManager::instance().clearMemory();
    waitUntil([] { return decodePoolIdle(); }, kPoolIdleCapMs);
    ImageRepository::instance().invalidateAll();
    CacheManager::instance().clearMemory();
}

void issueNavigateNext()
{
    CommandRegistry::instance().execute("navigate_next");
}

QString expectedNext(ImageListModel *list, const QString &current)
{
    if (!list || list->count() < 2)
        return {};
    const int idx = list->indexOf(current);
    const int next = idx < 0 ? 0 : (idx + 1) % list->count();
    return list->pathAt(next);
}

struct E2eHost
{
    QTemporaryDir subset;
    std::unique_ptr<MainWindow> window;
    ImageViewer *viewer = nullptr;
    ImageListModel *list = nullptr;
    SelectionModel *selection = nullptr;
    int subsetLimit = 4;
    std::string error;

    bool prepareSubset(const Corpus &corpus)
    {
        if (!subset.isValid())
        {
            error = "temp subset dir failed";
            return false;
        }
        const auto &src = !corpus.jpegPaths.empty() ? corpus.jpegPaths : corpus.allPaths();
        const int limit = std::max(2, subsetLimit);
        const size_t n = std::min(src.size(), static_cast<size_t>(limit));
        int copied = 0;
        for (size_t i = 0; i < n; ++i)
        {
            const QString from = QString::fromStdString(src[i]);
            const QString name = QFileInfo(from).fileName();
            if (name.isEmpty())
                continue;
            if (QFile::copy(from, subset.filePath(name)))
                ++copied;
        }
        if (copied < 2)
        {
            error = "need >=2 subset imgs";
            return false;
        }
        return true;
    }

    QString currentImage() const
    {
        return selection ? selection->currentImage() : QString();
    }

    bool openGallery()
    {
        window = std::make_unique<MainWindow>();
        window->resize(1280, 800);
        window->show();
        pump();
        if (auto *meta = window->findChild<MetadataPanel *>())
            meta->show();
        window->openExternalTargets({subset.path()});
        list = window->findChild<ImageListModel *>();
        selection = window->findChild<SelectionModel *>();
        const bool listed =
            waitUntil([this] { return list && list->count() >= 2 && !currentImage().isEmpty(); },
                      kSetupTimeoutMs);
        if (!listed)
        {
            error = "gallery did not list >=2 images";
            return false;
        }
        return true;
    }

    bool openViewer()
    {
        const QString first = currentImage();
        window->onImageOpen(first);
        viewer = findViewer();
        const bool shown = waitUntil(
            [this, first]
            {
                if (!viewer)
                    viewer = findViewer();
                return viewerShows(viewer, first);
            },
            kSetupTimeoutMs);
        if (!shown || !viewer)
        {
            error = "viewer did not present the first image";
            return false;
        }
        if (viewer->width() < 64 || viewer->height() < 64)
            viewer->resize(1280, 800);
        forcePaint(viewer);
        return true;
    }

    bool start(const Corpus &corpus, int subsetMax)
    {
        subsetLimit = subsetMax;
        qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
        qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
        auto *gui = qobject_cast<QApplication *>(QCoreApplication::instance());
        if (!gui)
        {
            error = "need QApplication (offscreen)";
            return false;
        }
        gui->setQuitOnLastWindowClosed(false);
        if (!prepareSubset(corpus) || !openGallery() || !openViewer())
            return false;
        return true;
    }

    void warmup(int passes, int timeoutMs)
    {
        ImageRepository::LoadOptions opts;
        opts.generateHistogram = false;
        if (list)
        {
            for (const QString &path : list->paths())
                ImageRepository::instance().load(path.toUtf8().toStdString(), opts);
        }
        int n = list ? list->count() : 0;
        if (passes >= 0)
            n = std::min(n, passes);
        for (int i = 0; i < n; ++i)
            (void)switchNext(timeoutMs);
    }

    void evictForCold()
    {
        evictCaches();
    }

    bool switchNext(int timeoutMs)
    {
        if (!viewer || !list || !window)
            return false;
        const QString expected = expectedNext(list, viewer->currentPath());
        if (expected.isEmpty())
            return false;
        issueNavigateNext();
        // The viewer path updates synchronously; the frame/raster does not.
        // Waiting on that display (not a signal latch, and not a fixed sleep)
        // returns as soon as the new image is on screen.
        const bool ok =
            waitUntil([this, expected] { return viewerShows(viewer, expected); }, timeoutMs);
        if (ok)
            forcePaint(viewer);
        return ok;
    }
};

ScenarioResult runE2eSwitch(const Corpus &corpus, bool warm, int iterations, int subsetMax,
                            int warmupPasses, int timeoutMs, const char *id, const char *metric)
{
    ScenarioResult r;
    r.name = id;
    r.metric = metric;
    r.passed = true; // harness overwrites this only when a budget gates the metric

    if (iterations < 1)
        iterations = 1;
    if (corpus.allPaths().size() < 2)
        return ScenarioResult{id, metric, kUnmeasuredMs, {}, "need >=2 imgs", true};

    E2eHost host;
    if (!host.start(corpus, subsetMax))
        return ScenarioResult{id, metric, kUnmeasuredMs, {}, host.error, true};

    if (warm)
        host.warmup(warmupPasses, timeoutMs);

    std::vector<double> samples;
    samples.reserve(static_cast<size_t>(iterations));
    int timeouts = 0;
    for (int i = 0; i < iterations; ++i)
    {
        if (!warm)
            host.evictForCold();
        const double t0 = nowMs();
        const bool ok = host.switchNext(timeoutMs);
        samples.push_back(nowMs() - t0);
        if (!ok)
            ++timeouts;
    }

    r.timing = summarize(samples);
    r.value = r.timing.p50Ms;
    std::ostringstream d;
    d << (warm ? "warm" : "cold") << " n=" << iterations
      << " subset=" << (host.list ? host.list->count() : 0) << " timeouts=" << timeouts
      << " p50=" << r.timing.p50Ms << " p95=" << r.timing.p95Ms << " p99=" << r.timing.p99Ms
      << " (UI navigate_next → present+paint)";
    r.detail = d.str();
    return r;
}

E2eSchedule fullE2eSchedule()
{
    E2eSchedule s;
    s.label = "full";
    s.warm = true;
    s.cold = true;
    s.warmIterations = 100;
    s.coldIterations = 30;
    s.subsetMax = 50;
    s.warmupPasses = -1;
    s.warmTimeoutMs = 1000;
    s.coldTimeoutMs = 2000;
    return s;
}

E2eSchedule shortE2eSchedule(bool smoke)
{
    E2eSchedule s;
    s.label = smoke ? "smoke" : "short";
    s.warmIterations = smoke ? 2 : 12;
    s.coldIterations = smoke ? 2 : 12;
    s.subsetMax = 4;
    s.warmupPasses = -1;
    s.warmTimeoutMs = 800;
    s.coldTimeoutMs = 1200;
    return s;
}

} // namespace

E2eSchedule E2eSchedule::full()
{
    return fullE2eSchedule();
}

E2eSchedule scheduleE2eSwitch(const E2eRequest &req)
{
    const bool explicitWarm = req.e2eFlag || req.tokenWarm;
    const bool explicitCold = req.e2eFlag || req.tokenCold;
    const bool anyExplicit = explicitWarm || explicitCold;
    if (req.smoke)
    {
        if (!anyExplicit)
            return {};
        E2eSchedule s = shortE2eSchedule(true);
        s.warm = explicitWarm;
        s.cold = explicitCold;
        return s;
    }
    if (anyExplicit)
    {
        E2eSchedule s = fullE2eSchedule();
        s.warm = explicitWarm;
        s.cold = explicitCold;
        return s;
    }
    const bool defaultWarm = !req.filterActive;
    const bool defaultCold = !req.filterActive;
    if (req.enforce)
    {
        if (!req.budgetWarm && !req.budgetCold)
            return {};
        E2eSchedule s = shortE2eSchedule(false);
        s.warm = req.budgetWarm && defaultWarm;
        s.cold = req.budgetCold && defaultCold;
        if (!s.warm && !s.cold)
            return {};
        return s;
    }
    E2eSchedule s = fullE2eSchedule();
    s.warm = defaultWarm;
    s.cold = defaultCold;
    if (!s.warm && !s.cold)
        return {};
    return s;
}

ScenarioResult scenarioE2eSwitchWarm(const Corpus &corpus, const E2eSchedule &run)
{
    return runE2eSwitch(corpus, true, run.warmIterations, run.subsetMax, run.warmupPasses,
                        run.warmTimeoutMs, "B18", "e2e_switch_warm_p50_ms");
}

ScenarioResult scenarioE2eSwitchCold(const Corpus &corpus, const E2eSchedule &run)
{
    return runE2eSwitch(corpus, false, run.coldIterations, run.subsetMax, run.warmupPasses,
                        run.coldTimeoutMs, "B19", "e2e_switch_cold_p50_ms");
}

} // namespace mviewer::bench
