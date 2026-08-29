// M55 bounded interactive-state regressions.
//
// This is intentionally a small real-widget test: it drives the same
// ThumbnailPanel/QPixmap path as Browse, then verifies that rapid navigation
// cannot grow the UI-owned raster cache without a hard count/byte bound.
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

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 2);
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QApplication::processEvents(QEventLoop::AllEvents, 2);
    return predicate();
}

bool makeCorpus(const QString &root, int count)
{
    if (!QDir().mkpath(root))
        return false;
    QImage seed(512, 512, QImage::Format_RGB32);
    for (int y = 0; y < seed.height(); ++y)
    {
        auto *line = reinterpret_cast<QRgb *>(seed.scanLine(y));
        for (int x = 0; x < seed.width(); ++x)
            line[x] = qRgb((x * 13 + y * 7) % 256, (x * 3 + y * 17) % 256,
                           (x * 19 + y * 5) % 256);
    }
    const QString seedPath = QDir(root).filePath(QStringLiteral("seed.png"));
    if (!seed.save(seedPath, "PNG"))
        return false;
    for (int i = 0; i < count; ++i)
    {
        const QString path = QDir(root).filePath(
            QStringLiteral("img_%1.png").arg(i, 5, 10, QLatin1Char('0')));
        if (!QFile::copy(seedPath, path))
            return false;
    }
    QFile::remove(seedPath);
    return true;
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir temp;
    if (!temp.isValid())
        return 2;
    const QString corpus = QDir(temp.path()).filePath(QStringLiteral("corpus"));
    if (!makeCorpus(corpus, 420))
        return 2;

    ThumbnailPipeline::instance().clear();
    ThumbnailPanel panel;
    panel.resize(900, 620);
    panel.setThumbSize(512);
    panel.show();
    QApplication::processEvents(QEventLoop::AllEvents, 5);
    panel.setDirectory(corpus);
    check(waitUntil([&] { return panel.pathList().size() == 420; }, 30000),
          "M55 corpus publishes all rows");

    // Visit many disjoint windows. This exercises row invalidation, QPixmap
    // conversion and eviction without manufacturing private cache state.
    if (panel.pathList().size() == 420)
    {
        QScrollBar *bar = panel.verticalScrollBar();
        const int maximum = bar ? bar->maximum() : 0;
        for (int i = 0; i <= 80; ++i)
        {
            if (bar)
                bar->setValue(maximum * i / 80);
            QApplication::processEvents(QEventLoop::AllEvents, 4);
        }
        TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool,
                                        std::chrono::seconds(60));
        waitUntil([&] { return panel.thumbReadyCount() > 0; }, 5000);
    }

    check(panel.thumbReadyCount() <= ThumbnailPanel::kThumbPixmapCacheMaxEntries,
          "M55 QPixmap cache respects the entry bound");
    check(panel.thumbReadyBytes() <= ThumbnailPanel::kThumbPixmapCacheMaxBytes,
          "M55 QPixmap cache respects the byte bound");
    check(panel.thumbReadyCount() > 0, "M55 real panel delivered display pixmaps");

    panel.setThumbSize(140);
    QApplication::processEvents(QEventLoop::AllEvents, 5);
    check(panel.thumbReadyCount() == 0 && panel.thumbReadyBytes() == 0,
          "thumbnail-size change drops stale QPixmap identity state");
    ThumbnailPipeline::instance().clear();
    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::seconds(30));

    std::printf("M55 interactive tests: %s\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
