// Metadata overlay layout acceptance (View > Image &info, I / M).
//
// The on-image info panel must size itself from its content: long values wrap
// instead of being clipped by a fixed-width box, and the whole box (title, rows,
// histogram, hint) stays inside the viewer.
#include "core/compare/Histogram.h"
#include "metadataoverlay.h"

#include <QApplication>
#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QWidget>

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
    } while (0)

void pump(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

template <typename Pred> bool pumpUntil(Pred pred, int timeoutMs)
{
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < timeoutMs)
    {
        if (pred())
            return true;
        pump(10);
    }
    return pred();
}

void checkInside(const MetadataOverlay::Layout &lay, const QRect &viewer)
{
    CHECK(lay.box.isValid() && viewer.contains(lay.box), "overlay box stays inside the viewer");
    CHECK(lay.box.width() >= 300, "overlay box keeps a readable minimum width");
    CHECK(!lay.title.isEmpty() && lay.box.contains(lay.title), "title line stays inside the box");
    CHECK(lay.box.contains(lay.hint), "hint line stays inside the box");
    for (int i = 0; i < lay.lines.size(); ++i)
        CHECK(lay.box.contains(lay.lines.at(i)), "every metadata line stays inside the box");
    if (lay.histogram.isValid())
        CHECK(lay.box.contains(lay.histogram), "histogram stays inside the box");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QCoreApplication::setOrganizationName("mviewer-metadata-overlay-test");
    QCoreApplication::setApplicationName("mviewer-metadata-overlay-test");

    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::cerr << "no temp dir\n";
        return 1;
    }

    QWidget host;
    host.resize(1100, 720);
    host.show(); // the overlay is visibility-gated: a hidden host drops the delivery
    MetadataOverlay overlay(&host);

    // A long path is what used to overflow the fixed-width box.
    const QString name =
        QString("a_quite_long_image_file_name") + QString(80, QChar('x')) + QStringLiteral(".png");
    const QString path = QDir(tmp.path()).filePath(name);
    QImage image(16, 16, QImage::Format_RGB32);
    image.fill(QColor(1, 2, 3));
    image.save(path, "PNG");

    overlay.showForImage(path);
    const bool ready =
        pumpUntil([&] { return !overlay.layout(QSize(1100, 720)).lines.isEmpty(); }, 5000);
    CHECK(ready, "overlay receives metadata for the current image");
    if (!ready)
    {
        std::cout << "metadata_overlay_tests: FAIL\n";
        return 1;
    }

    // A delivered histogram is laid out from the same box, never under the hint.
    mviewer::core::Histogram hist;
    hist.bins = 256;
    hist.r.assign(256, 4);
    hist.g.assign(256, 3);
    hist.b.assign(256, 2);
    overlay.setHistogram(hist);

    const QRect viewer(0, 0, 1100, 720);
    const MetadataOverlay::Layout lay = overlay.layout(viewer.size());
    checkInside(lay, viewer);
    CHECK(lay.histogram.isValid() && !lay.histogram.intersects(lay.hint),
          "histogram never overlaps the hint line");

    const QRect narrowViewer(0, 0, 420, 720);
    checkInside(overlay.layout(narrowViewer.size()), narrowViewer);

    // A viewer exactly as tall as the full box: the histogram yields its height
    // before the metadata is pushed off the window.
    const QRect snugViewer(0, 0, 1100, lay.box.height());
    const MetadataOverlay::Layout snug = overlay.layout(snugViewer.size());
    checkInside(snug, snugViewer);
    CHECK(snug.histogram.height() <= lay.histogram.height(),
          "histogram yields its height on a short viewer");

    if (g_failures > 0)
    {
        std::cout << "metadata_overlay_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "metadata_overlay_tests: PASS\n";
    return 0;
}
