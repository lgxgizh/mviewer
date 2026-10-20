#include "benchmark/scenarios.h"

#include "core/cache/CacheManager.h"
#include "core/command/CommandRegistry.h"
#include "core/image/ImageRepository.h"
#include "core/scheduler/TaskScheduler.h"
#include "imagelistmodel.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadatapanel.h"

#include <QApplication>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>
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
// Report-only: not in performance_budget.json scenario_map. Timeouts are
// counted in `detail` rather than hanging; `passed` stays true so --enforce
// does not fail on uncalibrated hardware.

namespace mviewer::bench
{

namespace
{

constexpr int kSubsetMax = 50;
constexpr int kSetupTimeoutMs = 15000;
constexpr int kWarmTimeoutMs = 2000;
constexpr int kColdTimeoutMs = 5000;
constexpr int kDrainMs = 2000;

void pump(int sliceMs = 16)
{
    QCoreApplication::processEvents(QEventLoop::AllEvents, sliceMs);
}

bool waitUntil(const std::function<bool()> &ready, int timeoutMs)
{
    const double deadline = nowMs() + static_cast<double>(timeoutMs);
    while (nowMs() < deadline)
    {
        if (ready())
            return true;
        pump();
    }
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

void evictCaches()
{
    ImageRepository::instance().invalidateAll();
    CacheManager::instance().clearMemory();
    TaskScheduler::instance().drain(TaskScheduler::DecodePool, std::chrono::milliseconds(kDrainMs));
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
    QString readyPath;
    QString error;

    bool prepareSubset(const Corpus &corpus)
    {
        if (!subset.isValid())
        {
            error = "temp subset dir failed";
            return false;
        }
        const auto &src = !corpus.jpegPaths.empty() ? corpus.jpegPaths : corpus.allPaths();
        const size_t n = std::min(src.size(), static_cast<size_t>(kSubsetMax));
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

    bool openGallery()
    {
        window = std::make_unique<MainWindow>();
        window->resize(1280, 800);
        window->show();
        pump(40);
        if (auto *meta = window->findChild<MetadataPanel *>())
            meta->show();
        window->openExternalTargets({subset.path()});
        list = window->findChild<ImageListModel *>();
        const bool listed = waitUntil(
            [this] { return list && list->count() >= 2 && !window->currentImagePath().isEmpty(); },
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
        const QString first = window->currentImagePath();
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
        QObject::connect(viewer, &ImageViewer::imageReady, viewer,
                         [this](const std::shared_ptr<ImageFrame> &)
                         {
                             if (viewer)
                                 readyPath = viewer->currentPath();
                         });
        QObject::connect(viewer, &ImageViewer::displayReady, viewer,
                         [this](const QSize &)
                         {
                             if (viewer)
                                 readyPath = viewer->currentPath();
                         });
        forcePaint(viewer);
        return true;
    }

    bool start(const Corpus &corpus)
    {
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

    void warmup()
    {
        ImageRepository::LoadOptions opts;
        opts.generateHistogram = false;
        if (list)
        {
            for (const QString &path : list->paths())
                ImageRepository::instance().load(path.toUtf8().toStdString(), opts);
        }
        const int n = list ? list->count() : 0;
        for (int i = 0; i < n; ++i)
            (void)switchNext(kWarmTimeoutMs);
    }

    void evictForCold()
    {
        evictCaches();
        pump(8);
        evictCaches();
    }

    bool switchNext(int timeoutMs)
    {
        if (!viewer || !list || !window)
            return false;
        const QString expected = expectedNext(list, viewer->currentPath());
        if (expected.isEmpty())
            return false;
        readyPath.clear();
        issueNavigateNext();
        const bool ok = waitUntil(
            [this, expected]
            { return pathEq(readyPath, expected) && viewerShows(viewer, expected); }, timeoutMs);
        if (ok)
            forcePaint(viewer);
        return ok;
    }
};

ScenarioResult runE2eSwitch(const Corpus &corpus, bool warm, int iterations, const char *id,
                            const char *metric)
{
    ScenarioResult r;
    r.name = id;
    r.metric = metric;
    r.passed = true; // report-only: never fail --enforce

    if (iterations < 1)
        iterations = 1;
    if (corpus.allPaths().size() < 2)
        return ScenarioResult{id, metric, 0, {}, "need >=2 imgs", true};

    E2eHost host;
    if (!host.start(corpus))
        return ScenarioResult{id, metric, 0, {}, host.error, true};

    if (warm)
        host.warmup();

    const int timeoutMs = warm ? kWarmTimeoutMs : kColdTimeoutMs;
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
      << " (UI navigate_next → present+paint; report-only)";
    r.detail = d.str();
    return r;
}

} // namespace

ScenarioResult scenarioE2eSwitchWarm(const Corpus &corpus, int iterations)
{
    return runE2eSwitch(corpus, true, iterations, "B18", "e2e_switch_warm_p50_ms");
}

ScenarioResult scenarioE2eSwitchCold(const Corpus &corpus, int iterations)
{
    return runE2eSwitch(corpus, false, iterations, "B19", "e2e_switch_cold_p50_ms");
}

} // namespace mviewer::bench
