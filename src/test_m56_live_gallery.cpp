// M56: row-local gallery mutation keeps selection/current/model identity.
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include "core/filesystem/DirectorySnapshot.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "core/scheduler/TaskScheduler.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QTemporaryDir>

#include <chrono>
#include <cstdio>
#include <functional>

namespace
{
int failures = 0;

void check(bool condition, const char *message)
{
    if (condition)
        std::printf("PASS: %s\n", message);
    else
    {
        std::printf("FAIL: %s\n", message);
        ++failures;
    }
}

void pump(int ms = 10)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 5);
    while (timer.elapsed() < ms);
}

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 15000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(10);
    return predicate();
}

bool writeImage(const QString &path, int width, int height, QRgb color)
{
    QImage image(width, height, QImage::Format_RGB32);
    image.fill(color);
    return image.save(path, "PNG");
}

bool removeEventually(const QString &path)
{
    for (int attempt = 0; attempt < 100; ++attempt)
    {
        if (QFile::remove(path))
            return true;
        pump(10);
    }
    return false;
}

std::string pathUtf8(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path)).toUtf8().toStdString();
}
} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    const QDir folder(temporary.path());
    const QString a = folder.filePath("a.png");
    const QString b = folder.filePath("b.png");
    const QString c = folder.filePath("c.png");
    if (!writeImage(a, 20, 20, qRgb(1, 2, 3)) || !writeImage(b, 24, 20, qRgb(4, 5, 6)) ||
        !writeImage(c, 28, 20, qRgb(7, 8, 9)))
        return 2;

    ThumbnailPipeline::instance().clear();
    ThumbnailPanel panel;
    SelectionModel selection;
    panel.setSelectionModel(&selection);
    panel.resize(700, 420);
    panel.show();
    panel.setSortMode(ThumbnailPanel::SortName);
    panel.setSortAscending(true);
    panel.setDirectory(temporary.path());
    check(waitUntil([&] { return panel.pathList().size() == 3; }),
          "gallery publishes its initial image sequence");
    panel.selectPath(b);
    check(waitUntil([&] { return selection.currentImage() == b; }),
          "shared selection follows the focused gallery item");

    int modelResets = 0;
    QObject::connect(panel.model(), &QAbstractItemModel::modelReset,
                     [&] { ++modelResets; });

    auto before = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()));
    check(writeImage(folder.filePath("0.png"), 22, 20, qRgb(10, 11, 12)),
          "write a lexically earlier image");
    auto after = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), before.generation + 1);
    before.path = pathUtf8(panel.currentDir());
    after.path = before.path;
    auto delta = mviewer::core::diffDirectorySnapshots(before, after);
    panel.applyDirectoryDelta(delta);
    check(panel.pathList().size() == 4 &&
              QFileInfo(panel.pathList().front()).fileName() == "0.png",
          "addition is inserted into the active sort order");
    check(selection.currentImage() == b && panel.pathList().contains(b),
          "inserting before current preserves current identity");
    check(modelResets == 0, "incremental addition does not reset the model");

    const QString renamed = folder.filePath("b-renamed.png");
    auto renameBefore = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 2);
    check(QFile::rename(b, renamed), "rename the selected image");
    auto renameAfter = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 3);
    renameBefore.path = pathUtf8(panel.currentDir());
    renameAfter.path = renameBefore.path;
    panel.applyDirectoryDelta(mviewer::core::diffDirectorySnapshots(renameBefore, renameAfter));
    check(!panel.pathList().contains(b) && panel.pathList().contains(renamed),
          "rename migrates the row path without a full rebuild");
    check(selection.currentImage() == renamed, "rename preserves shared current selection");
    check(modelResets == 0, "rename does not reset the model");

    auto modifyBefore = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 4);
    check(writeImage(renamed, 48, 48, qRgb(90, 80, 70)), "overwrite the current image");
    auto modifyAfter = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 5);
    modifyBefore.path = pathUtf8(panel.currentDir());
    modifyAfter.path = modifyBefore.path;
    int modifiedSignals = 0;
    QObject::connect(&panel, &ThumbnailPanel::pathsModified,
                     [&](const QStringList &) { ++modifiedSignals; });
    panel.applyDirectoryDelta(mviewer::core::diffDirectorySnapshots(modifyBefore, modifyAfter));
    check(modifiedSignals == 1 && selection.currentImage() == renamed,
          "overwrite invalidates the source while preserving current identity");
    check(modelResets == 0, "overwrite does not reset the model");

    auto removeBefore = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 6);
    check(removeEventually(renamed), "remove the current image");
    auto removeAfter = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 7);
    removeBefore.path = pathUtf8(panel.currentDir());
    removeAfter.path = removeBefore.path;
    panel.applyDirectoryDelta(mviewer::core::diffDirectorySnapshots(removeBefore, removeAfter));
    check(!panel.pathList().contains(renamed) && selection.currentImage() != renamed,
          "current-source removal advances to an available neighbor");
    check(modelResets == 0, "removal remains row-local");

    auto unavailable = mviewer::core::DirectorySnapshot{};
    unavailable.path = pathUtf8(panel.currentDir());
    const auto unavailableDelta =
        mviewer::core::diffDirectorySnapshots(removeAfter, unavailable);
    panel.applyDirectoryDelta(unavailableDelta);
    check(panel.pathList().size() == 3 && panel.pathList().contains(a),
          "temporary directory loss keeps the last coherent gallery visible");
    auto recovered = mviewer::core::snapshotDirectory(pathUtf8(temporary.path()), 8);
    recovered.path = unavailable.path;
    panel.applyDirectoryDelta(mviewer::core::diffDirectorySnapshots(unavailable, recovered));
    check(panel.pathList().size() == 3 && !panel.pathList().contains(renamed),
          "directory recovery replaces stale rows with the authoritative snapshot");

    ThumbnailPipeline::instance().clear();
    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(30));
    std::printf("M56 live gallery tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
