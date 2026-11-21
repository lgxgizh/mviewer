// Folder of four 4096x3072 8-bit JPEGs (12.6 MP, above the 4 MP LOD cutoff,
// same size class as the #370 hang). This is not PNG- or content-specific:
// some later rasters in a small folder must still appear after a directory
// sequence grows, after cancelled-then-restarted neighbor preloads, and
// after rapid current-image changes. A stuck first raster leaves
// isLoadingBusy() true (residual #373: m_largeSourcePending never clears).

#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"

#include <QApplication>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>

#include <cstdio>
#include <functional>
#include <thread>

static int g_failures = 0;

#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s\n", m);                                                          \
            std::fflush(stdout);                                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

namespace
{

constexpr int kWidth = 4096;
constexpr int kHeight = 3072;
constexpr int kReadyMs = 30000;

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

QStringList writeFourJpegs(const QTemporaryDir &tmp)
{
    QStringList paths;
    const QColor colors[] = {QColor(200, 30, 30), QColor(30, 180, 40), QColor(40, 80, 220),
                             QColor(200, 180, 20)};
    for (int i = 0; i < 4; ++i)
    {
        QImage img(kWidth, kHeight, QImage::Format_RGB888);
        img.fill(colors[i]);
        const QString path = tmp.filePath(QStringLiteral("img%1.jpg").arg(i + 1));
        if (!img.save(path, "JPEG", 90))
            return {};
        paths.push_back(path);
    }
    return paths;
}

bool waitLodReady(ImageViewer &viewer, const QString &path)
{
    return waitTrue(
        [&]
        {
            // isLoadingBusy() is private; a landed first raster sets isLodDisplay()
            // and clears m_loading / m_largeSourcePending together.
            return viewer.currentPath() == path && viewer.isLodDisplay();
        },
        kReadyMs);
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("FAIL: temp dir\n");
        return 1;
    }
    const QStringList paths = writeFourJpegs(tmp);
    CHECK(!paths.isEmpty() && paths.size() == 4, "four 4096x3072 JPEGs written");
    if (paths.size() != 4)
        return 1;

    // A. Open the four-file sequence and visit every image, including a
    //    mid-list jump that should promote an in-flight neighbor preload.
    {
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        pump(50);
        viewer.setBrowseSequence(paths);
        viewer.setImage(paths[0]);
        CHECK(waitLodReady(viewer, paths[0]), "A: first 12.6 MP JPEG displays");
        // Give neighbor preloads a moment to start, then visit each remaining
        // file, including going back to one that was cancelled mid-flight.
        pump(20);
        viewer.setImage(paths[1]);
        CHECK(waitLodReady(viewer, paths[1]), "A: second image appears after neighbor preload");
        viewer.setImage(paths[3]);
        CHECK(waitLodReady(viewer, paths[3]), "A: fourth image appears (not only adjacent ones)");
        viewer.setImage(paths[2]);
        CHECK(waitLodReady(viewer, paths[2]), "A: third image appears after a cancelled jump");
        viewer.setImage(paths[0]);
        CHECK(waitLodReady(viewer, paths[0]), "A: return to first image is still ready");
    }

    // B. Progressive folder growth (gallery scan publishes 1 path, then 4).
    //    setBrowseSequence bumps browse generation, used to cancel preloads
    //    and clear the warm cache. Every later raster must still appear.
    {
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        pump(50);
        viewer.setBrowseSequence({paths[0]});
        viewer.setImage(paths[0]);
        CHECK(waitLodReady(viewer, paths[0]), "B: first image ready with a one-file sequence");
        viewer.setBrowseSequence(paths);
        pump(10);
        for (int i = 1; i < 4; ++i)
        {
            viewer.setImage(paths[i]);
            CHECK(waitLodReady(viewer, paths[i]),
                  qPrintable(QStringLiteral("B: image %1 appears after sequence grew").arg(i + 1)));
        }
        CHECK(viewer.isLodDisplay(), "B: later raster settled on the LOD path");
    }

    // C. Same-list sequenceChanged churn (scan complete / filter rebuild)
    //    plus a rapid cancel-then-restart of the current image.
    {
        ImageViewer viewer;
        viewer.resize(1280, 800);
        viewer.show();
        pump(50);
        viewer.setBrowseSequence(paths);
        viewer.setImage(paths[0]);
        CHECK(waitLodReady(viewer, paths[0]), "C: first image ready");
        viewer.setBrowseSequence(paths);
        viewer.setBrowseSequence(paths);
        viewer.setImage(paths[1]);
        viewer.setImage(paths[2]);
        viewer.setImage(paths[1]);
        CHECK(waitLodReady(viewer, paths[1]), "C: cancelled-then-restarted neighbor still appears");
        viewer.setImage(paths[3]);
        CHECK(waitLodReady(viewer, paths[3]), "C: last image appears after sequence churn");
        CHECK(viewer.isLodDisplay(), "C: settled display is on the LOD path");
    }

    if (g_failures == 0)
        std::printf("OK: lod_four_jpeg_ready (%d images, 4096x3072)\n",
                    static_cast<int>(paths.size()));
    else
        std::printf("%d FAIL(s) in lod_four_jpeg_ready\n", g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
