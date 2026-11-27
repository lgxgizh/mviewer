// Regression: rotating an image in Browse mode used to leave that cell's
// thumbnail blank/white, and it did not recover on its own.
//
// Two refresh paths are exercised against a real rewritten file:
//   * the host's explicit refresh (ThumbnailProvider::invalidateSource +
//     ThumbnailPanel::invalidateSourceImage + refreshEntryDimensions)
//   * the live-folder delta (DirectorySnapshot diff -> applyDirectoryDelta),
//     which is what the directory watcher delivers when the file changes.
#include "core/filesystem/DirectorySnapshot.h"
#include "core/image/ImageFileRotate.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailpanel.h"
#include "thumbnailprovider.h"

#include <QApplication>
#include <QColor>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QPixmap>
#include <QTemporaryDir>

#include <functional>
#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
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

bool waitFor(const std::function<bool()> &pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (!pred() && t.elapsed() < timeoutMs)
        pump(10);
    return pred();
}

QString writePng(const QString &dirPath, const QString &name, const QColor &color)
{
    const QString path = QDir(dirPath).filePath(name);
    QImage image(64, 32, QImage::Format_RGB32);
    image.fill(color);
    // Markers so a flat fill cannot be mistaken for a real decode.
    image.setPixel(4, 4, qRgb(255, 255, 255));
    image.setPixel(59, 27, qRgb(0, 0, 0));
    return image.save(path, "PNG") ? path : QString();
}

int distinctColours(const QPixmap &pixmap)
{
    const QImage image = pixmap.toImage().convertToFormat(QImage::Format_RGB32);
    QSet<QRgb> colours;
    for (int y = 0; y < image.height(); y += 2)
        for (int x = 0; x < image.width(); x += 2)
            colours.insert(image.pixel(x, y));
    return colours.size();
}

bool samePixels(const QPixmap &a, const QPixmap &b)
{
    return a.toImage().convertToFormat(QImage::Format_RGB32) ==
           b.toImage().convertToFormat(QImage::Format_RGB32);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);

    QTemporaryDir dir;
    CHECK(dir.isValid(), "temp dir created");
    const QString red = writePng(dir.path(), "red.png", QColor(200, 30, 30));
    const QString green = writePng(dir.path(), "green.png", QColor(30, 200, 30));
    const QString blue = writePng(dir.path(), "blue.png", QColor(30, 30, 200));
    CHECK(!red.isEmpty() && !green.isEmpty() && !blue.isEmpty(), "fixtures written");

    ThumbnailPanel panel;
    panel.resize(640, 400);
    panel.show();
    pump(50);
    panel.setDirectory(dir.path());
    CHECK(waitFor([&] { return panel.pathList().size() == 3; }, 10000), "scan finished");

    const auto ready = [&](const QString &path) { return !panel.thumbReady(path).isNull(); };
    CHECK(waitFor([&] { return ready(red); }, 10000), "red thumbnail produced before rotation");
    const QPixmap originalThumb = panel.thumbReady(red);
    CHECK(distinctColours(originalThumb) > 1, "pre-rotate thumbnail is not a flat fill");

    const std::string utf8 = red.toUtf8().toStdString();
    const std::string dirUtf8 = dir.path().toUtf8().toStdString();

    // ---- Path 1: the host's explicit refresh ---------------------------------
    mviewer::core::ImageFileRotateResult rotated = mviewer::core::rotateImageFile(utf8, 90);
    CHECK(rotated.ok, "rotateImageFile succeeded");

    ThumbnailProvider::invalidateSource(utf8);
    panel.invalidateSourceImage(red);
    panel.refreshEntryDimensions({red});
    CHECK(waitFor([&] { return ready(red); }, 15000), "explicit refresh produces a thumbnail");
    const QPixmap explicitThumb = panel.thumbReady(red);
    CHECK(!samePixels(originalThumb, explicitThumb),
          "explicit refresh delivers the rotated pixels (not the stale pixmap)");

    // ---- Path 2: the live-folder delta ---------------------------------------
    // Rotate once more so the expected result differs from both earlier states,
    // then let a real DirectorySnapshot diff drive the panel exactly like the
    // directory watcher does.
    mviewer::core::DirectorySnapshot before =
        mviewer::core::snapshotDirectory(dirUtf8, 1);
    rotated = mviewer::core::rotateImageFile(utf8, 90);
    CHECK(rotated.ok, "second rotateImageFile succeeded");
    const mviewer::core::DirectorySnapshot after =
        mviewer::core::snapshotDirectory(dirUtf8, 2);
    const mviewer::core::DirectoryDelta delta =
        mviewer::core::diffDirectorySnapshots(before, after);
    CHECK(delta.hasImageChanges(), "the snapshot diff sees the rewritten file");
    panel.applyDirectoryDelta(delta);

    CHECK(waitFor([&] { return ready(red); }, 15000),
          "live delta produces a thumbnail again instead of leaving the cell blank");
    const QPixmap deltaThumb = panel.thumbReady(red);
    CHECK(distinctColours(deltaThumb) > 1, "live-delta thumbnail is not a flat white fill");
    CHECK(!samePixels(explicitThumb, deltaThumb) && !samePixels(originalThumb, deltaThumb),
          "live delta delivers the newest pixels rather than a stale cached pixmap");

    CHECK(waitFor([&] { return ready(green) && ready(blue); }, 10000),
          "neighbouring cells keep their thumbnails");

    // ---- Path 3: MainWindow's multi-pane refresh order -------------------------
    // A Details pane plus a hidden second pane on the same directory. The shared
    // source must be invalidated once; each pane then drops only its own cache.
    // (Invalidating per pane cancelled the decode the first pane had just enqueued
    // and left its cell as the AlternateBase placeholder forever.)
    ThumbnailPanel details;
    details.resize(640, 400);
    details.setViewMode(ThumbnailPanel::Details);
    details.show();
    pump(50);
    details.setDirectory(dir.path());
    ThumbnailPanel hiddenPane;
    hiddenPane.setDirectory(dir.path()); // never shown
    CHECK(waitFor([&] { return details.pathList().size() == 3; }, 10000),
          "details pane scanned the fixtures");
    CHECK(waitFor([&] { return !details.thumbReady(red).isNull(); }, 15000),
          "details pane produced a thumbnail");

    rotated = mviewer::core::rotateImageFile(utf8, 90);
    CHECK(rotated.ok, "third rotateImageFile succeeded");
    const QPixmap beforeMulti = details.thumbReady(red);

    ThumbnailProvider::invalidateSource(utf8); // exactly once for the batch
    details.dropLocalThumbnailCache(red);
    hiddenPane.dropLocalThumbnailCache(red);

    CHECK(waitFor([&] { return !details.thumbReady(red).isNull(); }, 15000),
          "visible pane recovers while a hidden pane shares the directory");
    const QPixmap afterMulti = details.thumbReady(red);
    CHECK(distinctColours(afterMulti) > 1,
          "the recovered details thumbnail is not a flat placeholder");
    CHECK(!samePixels(beforeMulti, afterMulti),
          "the visible pane shows the newest pixels after the multi-pane refresh");

    // ---- Path 4: a stale global pipeline size must not drop the callback ------
    // ThumbnailPipeline is a single shared instance, so another pane or an older
    // view mode can leave its size different from this panel's. Details scales any
    // thumbnail to its cell, so the result must still be accepted.
    ThumbnailPipeline::instance().setThumbSize(details.thumbSize() > 64 ? 64 : 128);
    rotated = mviewer::core::rotateImageFile(utf8, 90);
    CHECK(rotated.ok, "fourth rotateImageFile succeeded");
    ThumbnailProvider::invalidateSource(utf8);
    details.dropLocalThumbnailCache(red);
    CHECK(waitFor([&] { return !details.thumbReady(red).isNull(); }, 15000),
          "a mismatched pipeline thumb size does not lose the decoded thumbnail");
    CHECK(distinctColours(details.thumbReady(red)) > 1,
          "the mismatched-size thumbnail is not blank");

    if (g_failures != 0)
    {
        std::cout << "browse_rotate_thumbnail_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "browse_rotate_thumbnail_tests: PASS\n";
    return 0;
}
