// H23: object-level teardown stress (see docs/adr/017-process-lifetime-services.md).
//
// The suite contains several deliberate Qt-teardown workarounds: acceptance
// tests terminate the process instead of destroying their MainWindow, the
// workflow suite leaves its last window to process teardown after an offscreen
// QFileDialog, and process-wide services are intentionally leaked. Those exist
// for Qt's *global* teardown (static destruction after QCoreApplication is
// gone, DLL detach ordering) — which no test can fix and which is not a product
// defect.
//
// What a test CAN control is object-level teardown: create a real MainWindow,
// let it start the browse/thumbnail/metadata pipelines and a CompareWorkspace
// with queued decode+diff work, then destroy everything while that work is
// still in flight. That repeatedly crashed or hung historically. This test does
// it in a loop and asserts:
//   * every round finishes inside its time budget (no hang),
//   * no top-level widget survives a round (no leaked window),
//   * the last round still behaves like the first (no accumulated damage),
//   * CompareWorkspace survives being destroyed with queued work.
//
// It deliberately does not open QFileDialog/report flows: on the offscreen
// platform those leave internal modal widgets that deadlock MainWindow
// destruction (documented in test_workflow_ux.cpp), which is an environment
// property, not something this test should mask.

#include "compareworkspace.h"
#include "core/scheduler/TaskScheduler.h"
#include "directorytree.h"
#include "mainwindow.h"
#include "runtime_storage.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QSettings>
#include <QStandardPaths>
#include <QStringList>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <memory>
#include <string>
#include <vector>

namespace
{

int g_failures = 0;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::printf("FAIL: %s\n", message);                                                    \
            ++g_failures;                                                                          \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::printf("  ok: %s\n", message);                                                    \
        }                                                                                          \
    } while (false)

void pump(int ms)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

QString writePng(const QDir &dir, const QString &name, QColor color, int size)
{
    QImage img(size, size, QImage::Format_RGB32);
    img.fill(color);
    const QString path = dir.filePath(name);
    img.save(path, "PNG");
    return path;
}

// One create → use → destroy cycle of the real main window with the browse
// pipeline running. `budgetMs` bounds the whole cycle.
bool runWindowRound(const QString &dirPath, int budgetMs)
{
    QElapsedTimer timer;
    timer.start();

    {
        MainWindow window;
        window.resize(1024, 700);
        window.show();
        pump(60);

        // Start the real pipelines: directory scan, thumbnails, metadata index.
        auto *tree = window.findChild<DirectoryTree *>();
        ThumbnailPanel *panel = window.findChild<ThumbnailPanel *>();
        if (!tree || !panel)
            return false;
        tree->navigateTo(dirPath, true);

        // Wait (bounded) until the scan produced rows, so the metadata index and
        // thumbnail work this test wants in flight has genuinely started.
        QElapsedTimer scan;
        scan.start();
        while (panel->entries().isEmpty() && scan.elapsed() < 8000)
            pump(25);
        if (panel->entries().isEmpty())
            return false;
        pump(160); // leave preload/metadata work queued

        // Destroyed here, with thumbnail/metadata/preload work still queued.
    }

    // Any delivery that was already posted must land on a dead-but-guarded
    // receiver without touching freed memory.
    pump(80);
    return timer.elapsed() < budgetMs;
}

// Compare keeps its own async batches (pane materialization, diff, histogram);
// destroy the workspace while they are queued.
bool runCompareRound(const std::vector<std::string> &paths, int budgetMs)
{
    QElapsedTimer timer;
    timer.start();
    {
        auto workspace = std::make_unique<CompareWorkspace>();
        workspace->resize(900, 640);
        workspace->show();
        workspace->setImages(
            QStringList{QString::fromStdString(paths[0]), QString::fromStdString(paths[1])});
        pump(120);
        if (workspace->comparedImageCount() != 2)
            return false;
        workspace.reset();
    }
    pump(80);
    return timer.elapsed() < budgetMs;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // Deterministic persistence state: a real user/previous-run session would
    // make MainWindow asynchronously reopen folders and race this test.
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-teardown-stress-test");
    QCoreApplication::setApplicationName("mviewer-teardown-stress-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();
    {
        const QString cfg = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation);
        if (!cfg.isEmpty())
            QDir(cfg).removeRecursively();
    }

    // Offscreen platform: several workers running full-resolution
    // QImageReader::read() concurrently is a known Qt deadlock, so the pools are
    // serialized — this test is about teardown, not decode concurrency.
    auto &scheduler = TaskScheduler::instance();
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::UI, 1);
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::Thumbnail, 1);
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::Background, 1);

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("teardown_stress_tests: FAIL (no temporary directory)\n");
        return 1;
    }
    QDir dir(tmp.path());
    const QStringList names = {QStringLiteral("ts_000.png"), QStringLiteral("ts_001.png"),
                               QStringLiteral("ts_002.png"), QStringLiteral("ts_003.png")};
    const QList<QColor> colors = {QColor(180, 30, 30), QColor(30, 180, 30), QColor(30, 30, 180),
                                  QColor(180, 180, 30)};
    QStringList paths;
    for (int i = 0; i < names.size(); ++i)
        paths << writePng(dir, names[i], colors[i], 48);
    const std::vector<std::string> comparePaths = {paths[0].toStdString(), paths[1].toStdString()};

    // Baseline: the widgets the application itself owns (none, besides any the
    // platform creates lazily). Rounds are compared against the post-first-round
    // count so a lazily-created platform window does not fail the first round.
    constexpr int kRounds = 6;
    int baselineTopLevels = -1;
    for (int round = 0; round < kRounds; ++round)
    {
        const bool windowOk = runWindowRound(dir.absolutePath(), 30000);
        CHECK(windowOk, (std::string("round ") + std::to_string(round) +
                         ": MainWindow create/use/destroy completes inside its budget")
                            .c_str());

        const bool compareOk = runCompareRound(comparePaths, 30000);
        CHECK(compareOk, (std::string("round ") + std::to_string(round) +
                          ": CompareWorkspace destroyed with queued work completes")
                             .c_str());

        const int topLevels = QApplication::topLevelWidgets().size();
        if (baselineTopLevels < 0)
            baselineTopLevels = topLevels;
        CHECK(topLevels <= baselineTopLevels, (std::string("round ") + std::to_string(round) +
                                               ": no top-level window survives the round")
                                                  .c_str());
    }

    // The application must still be functional after all that teardown.
    {
        MainWindow window;
        window.show();
        pump(120);
        CHECK(window.isVisible(), "the application still creates and shows a window afterwards");
    }
    pump(60);
    CHECK(QApplication::topLevelWidgets().size() <= baselineTopLevels,
          "no window survives the final teardown");

    for (const QString &p : paths)
        QFile::remove(p);
    scheduler.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(5));

    if (g_failures > 0)
    {
        std::printf("teardown_stress_tests: FAIL (%d failures)\n", g_failures);
        return 1;
    }
    std::printf("teardown_stress_tests: PASS (%d rounds)\n", kRounds);
    return 0;
}
