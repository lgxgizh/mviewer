// P0 product-logic regressions:
//  * every rating/flag writer (keyboard, gallery context menu, metadata panel)
//    re-evaluates an active gallery rating/pick filter (single pipeline);
//  * an empty ImageViewer selection reaches the AnalysisPanel, and loading a
//    new image drops an unlocked ROI.
#include "directorymodel.h"
#include "directorytree.h"
#include "imageviewer.h"
#include "mainwindow.h"
#include "metadatapanel.h"
#include "runtime_storage.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
#include "widgets/ratingwidget.h"

#include "core/RatingStore.h"

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

void sendShortcut(QWidget *target, int key)
{
    QKeyEvent press(QEvent::KeyPress, key, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(target, &press);
    QKeyEvent release(QEvent::KeyRelease, key, Qt::ControlModifier | Qt::ShiftModifier);
    QApplication::sendEvent(target, &release);
    pump(30);
}

void testRatingPipeline(const QDir &dir, const QStringList &paths)
{
    std::cout << "── P0: rating/flag writers re-evaluate the gallery filter ──\n";
    MainWindow window;
    window.resize(1100, 750);
    window.show();
    pump(80);
    auto *tree = window.findChild<DirectoryTree *>();
    auto *directory = window.findChild<DirectoryModel *>();
    auto *panel = window.findChild<ThumbnailPanel *>();
    auto *selection = window.findChild<SelectionModel *>();
    auto *meta = window.findChild<MetadataPanel *>();
    CHECK(tree && directory && panel && selection && meta, "MainWindow exposes gallery objects");
    if (!tree || !directory || !panel || !selection || !meta)
        return;
    tree->navigateTo(dir.absolutePath(), true);
    CHECK(waitFor([&] { return panel->pathList().size() == paths.size(); }),
          "gallery lists every fixture");

    panel->setRatingFilter(3);
    CHECK(waitFor([&] { return panel->pathList().isEmpty(); }),
          "rating filter >= 3 hides unrated images");

    // Keyboard: Ctrl+Shift+4 on the current image.
    selection->setCurrentImage(paths[0]);
    pump(40);
    sendShortcut(&window, Qt::Key_4);
    CHECK(mviewer::core::RatingStore::instance().rating(paths[0].toUtf8().toStdString()) == 4,
          "keyboard rating writes RatingStore");
    CHECK(waitFor([&] { return panel->pathList() == QStringList{paths[0]}; }),
          "keyboard rating immediately re-evaluates the rating filter");

    // Gallery context menu (batch path).
    panel->selectPaths({paths[0]}, paths[0]);
    pump(30);
    panel->batchRateSelected(1);
    CHECK(waitFor([&] { return panel->pathList().isEmpty(); }),
          "context-menu rating drops the image out of the filter");

    // Metadata panel rating widget.
    selection->setCurrentImage(paths[1]);
    pump(40);
    meta->setImage(paths[1]);
    auto *stars = meta->findChild<RatingWidget *>();
    CHECK(stars != nullptr, "metadata panel exposes its rating widget");
    if (stars)
    {
        emit stars->ratingChanged(5);
        CHECK(waitFor([&] { return panel->pathList() == QStringList{paths[1]}; }),
              "metadata-panel rating re-evaluates the filter");
    }

    // Pick filter + keyboard Ctrl+Shift+P.
    panel->setRatingFilter(0);
    panel->setPickFilter(true);
    CHECK(waitFor([&] { return panel->pathList().isEmpty(); }), "pick filter hides unpicked");
    selection->setCurrentImage(paths[2]);
    pump(40);
    sendShortcut(&window, Qt::Key_P);
    CHECK(waitFor([&] { return panel->pathList() == QStringList{paths[2]}; }),
          "keyboard pick immediately re-evaluates the pick filter");
    panel->selectPaths({paths[2]}, paths[2]);
    pump(30);
    panel->batchSetFlagSelected(false, false);
    CHECK(waitFor([&] { return panel->pathList().isEmpty(); }),
          "context-menu unpick re-evaluates the pick filter");
    panel->clearFlagFilters();
    window.close();
    pump(40);
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
    mviewer::core::RatingStore::instance().setFilePath(
        root.filePath(QStringLiteral("ratings.txt")).toUtf8().toStdString());

    testRatingPipeline(dir, paths);
    testRoiResetOnNewImage(paths[0], paths[1]);

    std::cout << "=== P0 rating/ROI tests: " << (g_failures == 0 ? "PASS" : "FAIL") << " ("
              << g_failures << " failures) ===\n";
    return g_failures == 0 ? 0 : 1;
}
