// P0 product-logic regressions:
//  * an empty ImageViewer selection reaches the AnalysisPanel, and loading a
//    new image drops an unlocked ROI;
//  * a committed viewer ROI is stored in image pixels and follows pan.
#include "directorymodel.h"
#include "directorytree.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadatapanel.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QSettings>
#include <QStandardPaths>
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

QString writeImage(const QDir &dir, const QString &name, const QColor &color, int w = 64,
                   int h = 48)
{
    const QString path = dir.filePath(name);
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return QFileInfo(path).absoluteFilePath();
}

void dragSelection(ImageViewer &viewer)
{
    viewer.setSelectMode(true);
    const QPointF a(viewer.width() * 0.4, viewer.height() * 0.4);
    const QPointF b(viewer.width() * 0.6, viewer.height() * 0.6);
    QMouseEvent press(QEvent::MouseButtonPress, a, viewer.mapToGlobal(a), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &press);
    QMouseEvent move(QEvent::MouseMove, b, viewer.mapToGlobal(b), Qt::NoButton, Qt::LeftButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&viewer, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, b, viewer.mapToGlobal(b), Qt::LeftButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &release);
    pump(40);
}

void testRoiResetOnNewImage(const QString &a, const QString &b)
{
    std::cout << "── P0: new image drops an unlocked ROI and publishes an empty one ──\n";
    ImageViewer viewer;
    viewer.resize(640, 480);
    viewer.show();
    QList<QRect> published;
    QObject::connect(&viewer, &ImageViewer::selectionChanged,
                     [&published](const QRect &r) { published << r; });
    std::shared_ptr<ImageFrame> ready;
    QObject::connect(&viewer, &ImageViewer::imageReady,
                     [&ready](const std::shared_ptr<ImageFrame> &f) { ready = f; });
    viewer.setImage(a);
    CHECK(waitFor([&] { return ready != nullptr; }), "first image becomes ready");
    pump(60);
    dragSelection(viewer);
    CHECK(!published.isEmpty() && !published.last().isEmpty(), "drag publishes a non-empty ROI");
    published.clear();
    viewer.setSelectMode(false);
    viewer.setImage(b);
    CHECK(!published.isEmpty() && published.last().isEmpty(),
          "loading a new image publishes an empty selection");

    // Locked zoom keeps the ROI and re-measures it on the new frame.
    ready.reset();
    viewer.setImage(a);
    CHECK(waitFor([&] { return ready != nullptr; }), "image A reloads");
    pump(60);
    viewer.setLockZoom(true);
    dragSelection(viewer);
    viewer.setSelectMode(false);
    published.clear();
    ready.reset();
    viewer.setImage(b);
    CHECK(waitFor([&] { return ready != nullptr; }), "image B becomes ready under lock");
    pump(60);
    CHECK(!published.isEmpty() && !published.last().isEmpty(),
          "locked zoom keeps the ROI and re-publishes it for the new image");
    viewer.close();
    pump(30);
}

void testSelectionFollowsPan(const QString &path)
{
    std::cout << "── P0: committed ROI is image space and follows pan ──\n";
    ImageViewer viewer;
    viewer.resize(640, 480);
    viewer.show();
    std::shared_ptr<ImageFrame> ready;
    QObject::connect(&viewer, &ImageViewer::imageReady,
                     [&ready](const std::shared_ptr<ImageFrame> &f) { ready = f; });
    QList<QRect> published;
    QObject::connect(&viewer, &ImageViewer::selectionChanged,
                     [&published](const QRect &r) { published << r; });
    viewer.setImage(path);
    CHECK(waitFor([&] { return ready != nullptr; }), "image becomes ready for selection");
    pump(60);
    dragSelection(viewer);
    const QRect committed = viewer.committedSelectionForTest();
    CHECK(!committed.isEmpty(), "commit stores an image rect");
    CHECK(!published.isEmpty() && published.last() == committed,
          "selectionChanged matches the stored image rect");
    CHECK(committed.width() <= 64 && committed.height() <= 48,
          "stored rect is image pixels, not the widget drag");
    const QRect screenBefore = viewer.selectionScreenRectForTest();
    CHECK(!screenBefore.isEmpty(), "committed rect maps into the widget");

    const QPointF a(viewer.width() * 0.25, viewer.height() * 0.25);
    const QPointF b = a + QPointF(60, 40);
    QMouseEvent press(QEvent::MouseButtonPress, a, viewer.mapToGlobal(a), Qt::MiddleButton,
                      Qt::MiddleButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &press);
    QMouseEvent move(QEvent::MouseMove, b, viewer.mapToGlobal(b), Qt::NoButton, Qt::MiddleButton,
                     Qt::NoModifier);
    QApplication::sendEvent(&viewer, &move);
    QMouseEvent release(QEvent::MouseButtonRelease, b, viewer.mapToGlobal(b), Qt::MiddleButton,
                        Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &release);
    pump(20);

    CHECK(viewer.committedSelectionForTest() == committed, "pan leaves the image rect unchanged");
    const QRect screenAfter = viewer.selectionScreenRectForTest();
    CHECK(screenAfter.x() == screenBefore.x() + 60 && screenAfter.y() == screenBefore.y() + 40,
          "mapped selection shifts by the pan");
    CHECK(screenAfter.size() == screenBefore.size(), "pan does not resize the mapped selection");

    viewer.zoomIn();
    CHECK(viewer.committedSelectionForTest() == committed, "zoom leaves the image rect unchanged");
    const QRect screenZoomed = viewer.selectionScreenRectForTest();
    CHECK(screenZoomed.size() != screenAfter.size(), "zoom changes the mapped selection size");

    QKeyEvent esc(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(&viewer, &esc);
    CHECK(viewer.committedSelectionForTest().isEmpty(), "Esc clears the stored image rect");
    CHECK(viewer.selectionScreenRectForTest().isEmpty(), "Esc clears the mapped selection");
    pump(20);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    std::cout.setf(std::ios::unitbuf);
    qputenv("MVIEWER_DISABLE_UPDATE_CHECK", "1");
    qputenv("MVIEWER_DISABLE_RECOVERY_PROMPTS", "1");
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication::setOrganizationName("mviewer-p0-rating-roi-test");
    QCoreApplication::setApplicationName("mviewer-p0-rating-roi-test");
    mviewer::runtime::configureSettings();
    QSettings().clear();

    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    const QDir root(temporary.path());
    QDir().mkpath(root.filePath(QStringLiteral("gallery")));
    const QDir dir(root.filePath(QStringLiteral("gallery")));
    const QStringList paths{writeImage(dir, QStringLiteral("a.png"), QColor(200, 30, 30)),
                            writeImage(dir, QStringLiteral("b.png"), QColor(30, 200, 30)),
                            writeImage(dir, QStringLiteral("c.png"), QColor(30, 30, 200))};
    testRoiResetOnNewImage(paths[0], paths[1]);
    testSelectionFollowsPan(paths[0]);

    std::cout << "=== P0 ROI tests: " << (g_failures == 0 ? "PASS" : "FAIL") << " (" << g_failures
              << " failures) ===\n";
    return g_failures == 0 ? 0 : 1;
}
