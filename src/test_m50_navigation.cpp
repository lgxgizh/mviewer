// M50 Phase 0/1: directory-transition and asynchronous Sidecar convergence.
//
// This exercises the production MainWindow + DirectoryTree signal path rather
// than calling ThumbnailPanel::setDirectory directly. The test deliberately
// keeps a rating filter active so a missing post-import refresh is observable:
// the sidecar import must update the active gallery without blocking the
// initial directory transition.

#include "directorymodel.h"
#include "directorytree.h"
#include "mainwindow.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include "core/RatingStore.h"
#include "core/SidecarStore.h"
#include "core/scheduler/TaskScheduler.h"

#include <QAction>
#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QImage>
#include <QLineEdit>
#include <QSettings>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <chrono>
#include <functional>
#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                            \
    do                                                                                              \
    {                                                                                               \
        if (cond)                                                                                   \
            std::cout << "[ok] " << msg << "\n";                                                    \
        else                                                                                        \
        {                                                                                           \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";        \
            ++g_failures;                                                                           \
        }                                                                                           \
    } while (false)

void pump(int ms = 20)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 8000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(10);
    return predicate();
}

bool samePath(QString left, QString right)
{
    left = QDir::cleanPath(QDir::fromNativeSeparators(left));
    right = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return left.compare(right, Qt::CaseInsensitive) == 0;
#else
    return left == right;
#endif
}

QString writeImage(const QDir &dir, const QString &name, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage image(32, 24, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return QFileInfo(path).absoluteFilePath();
}

QAction *actionWithShortcut(const MainWindow &window, const QKeySequence &shortcut)
{
    for (QAction *action : window.findChildren<QAction *>())
        if (action->shortcut() == shortcut)
            return action;
    return nullptr;
}

bool navigate(DirectoryTree *tree, DirectoryModel *directory, const QString &path)
{
    if (!tree || !directory)
        return false;
    QElapsedTimer elapsed;
    elapsed.start();
    tree->navigateTo(path, true);
    const bool committed = waitFor([&] { return samePath(directory->currentDirectory(), path); });
    CHECK(elapsed.elapsed() < 500, "directory transition returns without waiting for the sidecar scan");
    return committed;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-m50-navigation-test");
    QCoreApplication::setApplicationName("mviewer-m50-navigation-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;

    const QDir root(temporary.path());
    const QString unicodeName = QStringLiteral("中文 空格 😀");
    const QString aPath = root.filePath(unicodeName + QStringLiteral("/目录-A"));
    const QString bPath = root.filePath(unicodeName + QStringLiteral("/目录-B"));
    QDir().mkpath(aPath);
    QDir().mkpath(bPath);
    const QDir dirA(aPath);
    const QDir dirB(bPath);
    const QString imageA = writeImage(dirA, QStringLiteral("样片 A.png"), QColor(30, 60, 90));
    const QString imageB = writeImage(dirB, QStringLiteral("样片 B.png"), QColor(90, 60, 30));

    auto &ratings = mviewer::core::RatingStore::instance();
    const QString ratingsPath = root.filePath(QStringLiteral("ratings.txt"));
    ratings.setFilePath(ratingsPath.toUtf8().toStdString());
    const std::string imageAUtf8 = imageA.toUtf8().toStdString();
    const std::string imageBUtf8 = imageB.toUtf8().toStdString();
    ratings.setRating(imageAUtf8, 5);
    ratings.setColorLabel(imageAUtf8, 3);
    ratings.setPicked(imageAUtf8, true);
    ratings.setRejected(imageAUtf8, true);
    ratings.flushSave();
    CHECK(mviewer::core::SidecarStore::instance().writeSidecar(imageAUtf8),
          "write a real Unicode sidecar fixture");
    ratings.setRating(imageAUtf8, 0);
    ratings.setColorLabel(imageAUtf8, 0);
    ratings.setPicked(imageAUtf8, false);
    ratings.setRejected(imageAUtf8, false);
    ratings.flushSave();
    CHECK(ratings.rating(imageAUtf8) == 0 && ratings.colorLabel(imageAUtf8) == 0 &&
              !ratings.picked(imageAUtf8) && !ratings.rejected(imageAUtf8),
          "clear in-store metadata before opening the directory");

    MainWindow window;
    window.resize(1100, 750);
    window.show();
    pump(80);
    auto *tree = window.findChild<DirectoryTree *>();
    auto *directory = window.findChild<DirectoryModel *>();
    auto *panel = window.findChild<ThumbnailPanel *>();
    auto *selection = window.findChild<SelectionModel *>();
    auto *pathEdit = window.findChild<QLineEdit *>(QStringLiteral("pathEdit"));
    QAction *back = actionWithShortcut(window, QKeySequence(QStringLiteral("Ctrl+Alt+Left")));
    QAction *forward = actionWithShortcut(window, QKeySequence(QStringLiteral("Ctrl+Alt+Right")));
    CHECK(tree && directory && panel && selection && pathEdit,
          "MainWindow exposes the production navigation SSOT objects");
    CHECK(back && forward, "directory back/forward actions are registered");
    if (!tree || !directory || !panel || !selection || !pathEdit || !back || !forward)
        return 1;

    panel->setRatingFilter(5);
    CHECK(navigate(tree, directory, aPath), "Unicode directory A commits through DirectoryTree");
    CHECK(waitFor([&] { return samePath(panel->currentDir(), aPath); }),
          "gallery follows the committed directory A");
    CHECK(waitFor([&] { return panel->pathList().size() == 1; }),
          "async sidecar import converges the active rating filter in directory A");
    CHECK(ratings.rating(imageAUtf8) == 5 && ratings.colorLabel(imageAUtf8) == 3 &&
              ratings.picked(imageAUtf8) && ratings.rejected(imageAUtf8),
          "sidecar import restores rating, label, picked, and rejected state");
    CHECK(selection->currentImage() == imageA,
          "sidecar convergence preserves the gallery current-image publication");

    CHECK(navigate(tree, directory, bPath), "directory B commits through the same owner");
    CHECK(waitFor([&] { return samePath(panel->currentDir(), bPath); }),
          "gallery follows directory B");
    CHECK(waitFor([&] { return panel->pathList().isEmpty(); }),
          "directory B does not inherit directory A's active rating filter result");
    CHECK(ratings.rating(imageBUtf8) == 0, "directory B has no stale rating import");

    back->trigger();
    CHECK(waitFor([&] { return samePath(directory->currentDirectory(), aPath); }),
          "directory Back restores A");
    CHECK(waitFor([&] { return panel->pathList().size() == 1; }),
          "Back re-runs Sidecar convergence for A");
    forward->trigger();
    CHECK(waitFor([&] { return samePath(directory->currentDirectory(), bPath); }),
          "directory Forward remains available after Back");
    CHECK(waitFor([&] { return panel->pathList().isEmpty(); }),
          "Forward restores B's filtered result");

    pathEdit->setText(QDir::toNativeSeparators(aPath));
    pathEdit->returnPressed();
    CHECK(waitFor([&] { return samePath(directory->currentDirectory(), aPath); }),
          "path edit commits through the same directory transition owner");

    window.close();
    auto &scheduler = TaskScheduler::instance();
    CHECK(scheduler.drain(TaskScheduler::PoolType::MetadataPool, std::chrono::seconds(10)),
          "Sidecar background work drains after MainWindow close");
    const auto metrics = scheduler.metrics(TaskScheduler::PoolType::MetadataPool);
    CHECK(metrics.pending == 0 && metrics.active_tasks == 0,
          "Sidecar scheduler pending and active counts converge to zero");

    if (g_failures > 0)
    {
        std::cout << "m50_navigation_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "m50_navigation_tests: PASS\n";
    return 0;
}
