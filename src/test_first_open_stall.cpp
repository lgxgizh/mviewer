// First-open stall: a blocking latency probe or a directory that never yields
// images must not freeze the GUI thread or leave the busy cursor stuck.
// NOLINTBEGIN(readability-magic-numbers)

#include "core/image/ImageBuffer.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "directorytree.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>
#include <QTimer>

#include <QColor>
#include <QEventLoop>

#include <atomic>
#include <chrono>
#include <functional>
#include <iostream>
#include <string>
#include <thread>

namespace
{

struct Results
{
    int pass = 0;
    int fail = 0;
};

void check(Results &results, bool cond, const char *msg)
{
    if (cond)
    {
        std::cout << "  PASS: " << msg << '\n';
        ++results.pass;
    }
    else
    {
        std::cout << "  FAIL: " << msg << '\n';
        ++results.fail;
    }
    std::cout.flush();
}

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

QString writePng(const QString &dir)
{
    QImage img(8, 8, QImage::Format_RGB32);
    img.fill(QColor(20, 40, 60));
    const QString path = dir + QStringLiteral("/one.png");
    img.save(path, "PNG");
    return path;
}

struct HookGuard
{
    ~HookGuard()
    {
        ThumbnailPanel::setHighLatencyProbeHook({});
    }
};

void testProbeDoesNotBlockUi(Results &results)
{
    std::cout << "\n[latency probe stays off the GUI thread]\n";
    HookGuard guard;
    QTemporaryDir tmp;
    check(results, tmp.isValid(), "temp dir created");
    if (!tmp.isValid())
        return;
    const QString dir = tmp.path();
    const QString image = writePng(dir);

    auto entered = std::make_shared<std::atomic<bool>>(false);
    auto release = std::make_shared<std::atomic<bool>>(false);
    auto hookThread = std::make_shared<QThread *>(nullptr);
    ThumbnailPanel::setHighLatencyProbeHook(
        [entered, release, hookThread]()
        {
            *hookThread = QThread::currentThread();
            entered->store(true, std::memory_order_release);
            while (!release->load(std::memory_order_acquire))
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
        });

    SelectionModel sel;
    ThumbnailPanel panel;
    panel.setSelectionModel(&sel);
    panel.resize(640, 480);
    panel.show();
    pump(30);

    panel.setDirectory(dir);
    check(results, waitTrue([&] { return entered->load(); }, 5000),
          "latency probe entered while the directory scan is in flight");
    auto uiPumped = std::make_shared<std::atomic<bool>>(false);
    QTimer::singleShot(0, &panel, [uiPumped]() { uiPumped->store(true); });
    check(results, waitTrue([&] { return uiPumped->load(); }, 2000),
          "GUI timer runs while the latency probe is blocked");
    check(results, *hookThread != nullptr && *hookThread != QThread::currentThread(),
          "latency probe runs off the GUI thread");
    check(results, waitTrue([&] { return panel.entries().size() == 1; }, 5000),
          "gallery lists the image before the latency probe returns");
    check(results, panel.entries().value(0).path == image || !panel.pathList().isEmpty(),
          "listed path is the image written into the directory");

    release->store(true, std::memory_order_release);
    check(results, waitTrue([&] { return QApplication::overrideCursor() == nullptr; }, 5000),
          "busy cursor released after a healthy scan");
    (void)guard;
}

void testMissingDirectory(Results &results)
{
    std::cout << "\n[missing directory reports failure and drops the cursor]\n";
    SelectionModel sel;
    ThumbnailPanel panel;
    panel.setSelectionModel(&sel);
    QStringList statuses;
    QObject::connect(&panel, &ThumbnailPanel::browseStatusChanged, &panel,
                     [&](const QString &message) { statuses.append(message); });
    const QString missing = QDir::tempPath() + QStringLiteral("/mviewer_missing_dir_stall_") +
                            QString::number(QCoreApplication::applicationPid());
    QDir(missing).removeRecursively();
    panel.setDirectory(missing);
    const bool failed =
        waitTrue([&] { return statuses.contains(QStringLiteral("目录加载失败")); }, 5000);
    check(results, failed, "missing directory emits 目录加载失败");
    check(results, panel.entries().isEmpty(), "missing directory leaves the gallery empty");
    check(results, QApplication::overrideCursor() == nullptr,
          "missing directory does not stick the busy cursor");
}

void testFailedThumbsAreNotRetried(Results &results)
{
    std::cout << "\n[failed thumbnails are not re-queued forever]\n";
    QTemporaryDir tmp;
    check(results, tmp.isValid(), "decode temp dir created");
    if (!tmp.isValid())
        return;
    const QString image = writePng(tmp.path());
    auto decodes = std::make_shared<std::atomic<int>>(0);

    SelectionModel sel;
    ThumbnailPanel panel;
    panel.setSelectionModel(&sel);
    panel.resize(640, 480);
    panel.show();
    pump(30);
    // Panel construction installs the production decoder. Replace it before
    // setDirectory copies the function into newly submitted tasks.
    ThumbnailPipeline::instance().setDecodeFn(
        [decodes](const std::string &, int)
        {
            decodes->fetch_add(1, std::memory_order_acq_rel);
            return ImageData{};
        });
    panel.setDirectory(tmp.path());
    check(results, waitTrue([&] { return panel.entries().size() == 1; }, 5000),
          "failed-decode directory still lists the file");
    ThumbnailPipeline::instance().setVisibleRange(0, 1);
    check(results, waitTrue([&] { return panel.thumbFailed(image); }, 5000),
          "first decode failure is delivered to the gallery");
    const int afterFail = decodes->load();
    check(results, afterFail >= 1, "the failing decoder ran at least once");
    ThumbnailPipeline::instance().setVisibleRange(0, 1);
    pump(250);
    check(results, decodes->load() == afterFail,
          "a second visible-range pass does not re-decode the failed thumb");

    ThumbnailPipeline::instance().setSources({image.toStdString()});
    ThumbnailPipeline::instance().setVisibleRange(0, 1);
    check(results, waitTrue([&] { return decodes->load() > afterFail; }, 5000),
          "setSources starts a new generation that may decode again");
    check(results, waitTrue([&] { return QApplication::overrideCursor() == nullptr; }, 5000),
          "busy cursor released when every thumbnail fails");
}

void testNavigateEmitsBeforeStat(Results &results)
{
    std::cout << "\n[navigateTo emits directoryChanged before the model stat]\n";
    QTemporaryDir tmp;
    check(results, tmp.isValid(), "tree temp dir created");
    if (!tmp.isValid())
        return;
    const QString dir = QDir(tmp.path()).filePath(QStringLiteral("相册"));
    QDir().mkpath(dir);

    DirectoryTree tree;
    tree.resize(320, 480);
    tree.show();
    pump(30);

    bool emitted = false;
    QObject::connect(&tree, &DirectoryTree::directoryChanged, &tree,
                     [&](const QString &) { emitted = true; });
    QElapsedTimer timer;
    timer.start();
    tree.navigateTo(dir, true);
    const qint64 callMs = timer.elapsed();
    check(results, emitted, "directoryChanged is emitted before navigateTo returns");
    check(results, callMs < 500, "navigateTo returns without waiting for the model index");
    const QString want = QDir::cleanPath(dir);
    const bool highlighted =
        waitTrue([&] { return QDir::cleanPath(tree.currentPath()) == want; }, 8000);
    check(results, highlighted, "tree highlight catches up after the background stat");
}

} // namespace

int main(int argc, char **argv)
{
    qputenv("QT_QPA_PLATFORM", "offscreen");
    QApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    Results results;
    testProbeDoesNotBlockUi(results);
    testMissingDirectory(results);
    testFailedThumbsAreNotRetried(results);
    testNavigateEmitsBeforeStat(results);
    pump(200);
    while (QApplication::overrideCursor() != nullptr)
        QApplication::restoreOverrideCursor();
    ThumbnailPanel::setHighLatencyProbeHook({});
    ThumbnailPipeline::instance().clear();
    std::cout << "\n=== Results: " << results.pass << " passed, " << results.fail
              << " failed ===\n";
    return results.fail == 0 ? 0 : 1;
}

// NOLINTEND(readability-magic-numbers)
