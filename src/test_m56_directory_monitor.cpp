// M56: active-directory debounce, stability retry, latest-wins and recovery.
#include "directorymonitor.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QTemporaryDir>

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

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs = 10000)
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
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 2;
    const QDir root(temporary.path());
    const QString folder = root.filePath(QStringLiteral("active"));
    if (!QDir().mkpath(folder) || !writeImage(QDir(folder).filePath("a.png"), 24, 24, qRgb(1, 2, 3)) ||
        !writeImage(QDir(folder).filePath("b.png"), 28, 24, qRgb(4, 5, 6)))
        return 2;

    DirectoryMonitor monitor;
    int deltaSignals = 0;
    int unavailableSignals = 0;
    bool lastAvailable = true;
    mviewer::core::DirectoryDelta lastDelta;
    QObject::connect(&monitor, &DirectoryMonitor::directoryDeltaReady,
                     [&](const mviewer::core::DirectoryDelta &delta)
                     {
                         ++deltaSignals;
                         lastDelta = delta;
                     });
    QObject::connect(&monitor, &DirectoryMonitor::directoryAvailabilityChanged,
                     [&](const QString &, bool available)
                     {
                         ++unavailableSignals;
                         lastAvailable = available;
                     });

    monitor.setActiveDirectory(folder);
    check(waitUntil([&] { return monitor.snapshot().available && monitor.snapshot().entries.size() == 2; }),
          "active directory establishes an asynchronous baseline");
    const int baselineScans = monitor.snapshotScanCount();

    const QString added = QDir(folder).filePath("c.png");
    check(writeImage(added, 32, 20, qRgb(7, 8, 9)), "write an added image");
    for (int i = 0; i < 100; ++i)
        monitor.notifyDirectoryChanged(folder);
    check(waitUntil([&] { return monitor.snapshot().entries.size() == 3 && deltaSignals >= 1; }),
          "coalesced notifications publish one stable addition");
    check(lastDelta.added.size() == 1 && lastDelta.added.front().path.ends_with("/c.png"),
          "addition delta names the changed source");
    const int afterAddScans = monitor.snapshotScanCount();
    check(afterAddScans - baselineScans <= 4,
          "a notification storm stays within the bounded scan budget");

    const QString renamed = QDir(folder).filePath("b-renamed.png");
    check(QFile::rename(QDir(folder).filePath("b.png"), renamed), "rename an image in place");
    monitor.notifyDirectoryChanged(folder);
    check(waitUntil([&]
                    {
                        return lastDelta.renamed.size() == 1 &&
                               lastDelta.renamed.front().after.path.ends_with("/b-renamed.png");
                    }),
          "unique file identity is delivered as a rename delta");

    const int stormBefore = monitor.snapshotScanCount();
    for (int i = 0; i < 1000; ++i)
        monitor.notifyDirectoryChanged(folder);
    check(waitUntil([&] { return monitor.snapshotScanCount() > stormBefore; }),
          "a no-op notification storm still performs a reconcile");
    pump(250);
    check(monitor.snapshotScanCount() - stormBefore <= 2,
          "1000 identical notifications do not trigger 1000 scans");

    check(QFile::remove(QDir(folder).filePath("a.png")) &&
              QFile::remove(QDir(folder).filePath("c.png")) && QFile::remove(renamed),
          "remove the active directory contents");
    check(QDir().rmdir(folder), "remove the active directory itself");
    monitor.notifyDirectoryChanged(folder);
    check(waitUntil([&] { return !monitor.snapshot().available && !lastAvailable; }),
          "directory disappearance is reported as unavailable");
    check(lastDelta.directoryUnavailable, "unavailable delta is explicit");

    check(QDir().mkpath(folder) &&
              writeImage(QDir(folder).filePath("restored.png"), 24, 24, qRgb(10, 11, 12)),
          "recreate the active directory");
    monitor.notifyDirectoryChanged(folder);
    check(waitUntil([&] { return monitor.snapshot().available && monitor.snapshot().entries.size() == 1; }),
          "a recreated directory can be reconciled without a navigation change");
    check(monitor.watcherHintCount() >= 1102 && monitor.snapshotScanCount() < 20,
          "monitor counters expose hints separately from bounded physical scans");
    check(unavailableSignals >= 2, "availability changes are observable at the boundary");

    std::printf("M56 directory monitor tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
