// M61 RED/closure gate: real linked-ROI interaction across Grid and Canvas.

#include "compareworkspace.h"
#include "core/image/ImageStats.h"
#include "core/image/SourceImage.h"
#include "core/scheduler/TaskScheduler.h"
#include "domain/SelectionInteraction.h"
#include "domain/SelectionMapping.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QDialog>
#include <QDir>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <chrono>
#include <cstdio>

namespace
{
int g_failures = 0;

#define CHECK(condition, message)                                                                  \
    do                                                                                             \
    {                                                                                              \
        if (!(condition))                                                                          \
        {                                                                                          \
            std::printf("FAIL: %s\n", message);                                                    \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

void pump(int ms = 30)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

bool waitFor(const std::function<bool()> &predicate, int timeoutMs = 5000)
{
    QElapsedTimer timer;
    timer.start();
    while (!predicate() && timer.elapsed() < timeoutMs)
        pump(20);
    return predicate();
}

QString writePattern(const QDir &dir, const QString &name, int width, int height, int bias)
{
    QImage image(width, height, QImage::Format_RGB32);
    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            image.setPixelColor(
                x, y,
                QColor((x * 7 + bias) & 0xff, (y * 11 + 20) & 0xff, (x * 3 + y * 5 + 40) & 0xff));
    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

void sourceRegionTruthTests(const QDir &dir)
{
    QImage image(128, 96, QImage::Format_RGB32);
    image.fill(QColor(40, 80, 120));
    const auto check =
        [&](const QString &name, const char *format, mviewer::core::SourceDecodePath expected)
    {
        const QString path = dir.filePath(name);
        if (!image.save(path, format))
            return;
        auto source = mviewer::core::SourceImage::open(path.toStdString());
        CHECK(source != nullptr, "source region truth fixture opens");
        if (!source)
            return;
        auto &counters = mviewer::core::SourceDecodeStats::instance().counters();
        counters.reset();
        CHECK(source->regionDecodePath() == expected,
              "region preflight classification matches format truth");
        if (expected == mviewer::core::SourceDecodePath::FullDecodeCrop)
        {
            CHECK(counters.fullDecodeCrop.load() == 0 && counters.fullDecode.load() == 0,
                  "unsupported region preflight performs no pixel decode");
            return;
        }
        const auto region = source->decodeRegion({8, 9, 20, 18}, 20, 18);
        CHECK(region.ok && region.decodePath == expected,
              "region decode classification matches evidence-backed format truth");
        CHECK(counters.fullDecode.load() == 0,
              "region capability path never invokes the explicit full decoder");
    };
    check(QStringLiteral("truth.jpg"), "JPG", mviewer::core::SourceDecodePath::BoundedRasterRegion);
    check(QStringLiteral("truth.png"), "PNG", mviewer::core::SourceDecodePath::FullDecodeCrop);
    check(QStringLiteral("truth.bmp"), "BMP", mviewer::core::SourceDecodePath::FullDecodeCrop);
#if defined(Q_OS_WIN)
    check(QStringLiteral("truth.tiff"), "TIFF",
          mviewer::core::SourceDecodePath::BoundedRasterRegion);
#endif
}

QCheckBox *findCheckBox(QWidget *root, const QString &prefix)
{
    for (QCheckBox *box : root->findChildren<QCheckBox *>())
        if (box->text().startsWith(prefix))
            return box;
    return nullptr;
}

RawImageView *pane(CompareWorkspace *workspace, int index)
{
    for (RawImageView *view : workspace->findChildren<RawImageView *>())
        if (view->cellIndex() == index)
            return view;
    return nullptr;
}

void sendRightDrag(QWidget *target, const QPoint &from, const QPoint &to, bool release = true)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), target->mapToGlobal(from),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), target->mapToGlobal(to), Qt::NoButton,
                     Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    pump(10);
    if (release)
    {
        QMouseEvent up(QEvent::MouseButtonRelease, QPointF(to), target->mapToGlobal(to),
                       Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(target, &up);
        pump(10);
    }
}

void clearROI(CompareWorkspace *workspace)
{
    QKeyEvent escape(QEvent::KeyPress, Qt::Key_Escape, Qt::NoModifier);
    QApplication::sendEvent(workspace, &escape);
    pump(20);
}

void domainAndCoreTests()
{
    using mviewer::domain::Selection;
    using mviewer::domain::SelectionHandle;
    const Selection origin{10, 10, 20, 20};
    const auto edit = [&](SelectionHandle handle, double x, double y)
    { return mviewer::domain::updateSelectionInteraction(origin, handle, 15, 15, x, y, 64, 64); };
    const Selection topLeft = edit(SelectionHandle::TopLeft, 5, 6);
    const Selection topRight = edit(SelectionHandle::TopRight, 35, 6);
    const Selection bottomLeft = edit(SelectionHandle::BottomLeft, 5, 36);
    const Selection bottomRight = edit(SelectionHandle::BottomRight, 35, 36);
    CHECK(topLeft.x == 5 && topLeft.y == 6 && topLeft.width == 25 && topLeft.height == 24,
          "top-left resize preserves the opposite corner");
    CHECK(topRight.x == 10 && topRight.y == 6 && topRight.width == 25 && topRight.height == 24,
          "top-right resize preserves the opposite corner");
    CHECK(bottomLeft.x == 5 && bottomLeft.y == 10 && bottomLeft.width == 25 &&
              bottomLeft.height == 26,
          "bottom-left resize preserves the opposite corner");
    CHECK(bottomRight.x == 10 && bottomRight.y == 10 && bottomRight.width == 25 &&
              bottomRight.height == 26,
          "bottom-right resize preserves the opposite corner");
    const Selection moved = mviewer::domain::updateSelectionInteraction(
        origin, SelectionHandle::Move, 15, 15, -50, -50, 64, 64);
    CHECK(moved.x == 0 && moved.y == 0 && moved.width == 20 && moved.height == 20,
          "moving an ROI clips to bounds without changing size");
    const Selection reverse = mviewer::domain::updateSelectionInteraction(
        {}, SelectionHandle::Create, 20, 20, 8, 7, 64, 64);
    CHECK(reverse.x == 8 && reverse.y == 7 && reverse.width == 12 && reverse.height == 13,
          "reverse creation is canonical");
    const Selection one = mviewer::domain::updateSelectionInteraction({}, SelectionHandle::Create,
                                                                      4.1, 5.1, 4.9, 5.9, 64, 64);
    CHECK(one.x == 4 && one.y == 5 && one.width == 1 && one.height == 1,
          "sub-pixel drag can persist a 1x1 ROI");

    const mviewer::domain::PresentationRect destination{13.25, 9.5, 717.0, 419.0};
    const Selection canonical{123, 77, 211, 99};
    const auto presentation =
        mviewer::domain::selectionToPresentation(canonical, destination, 1000, 800);
    const auto sourceA = mviewer::domain::presentationToSource({presentation.x, presentation.y},
                                                               destination, 1000, 800);
    const auto sourceB = mviewer::domain::presentationToSource(
        {presentation.x + presentation.width, presentation.y + presentation.height}, destination,
        1000, 800);
    const Selection roundTrip =
        mviewer::domain::normalizeSelection(sourceA.x, sourceA.y, sourceB.x, sourceB.y, 1000, 800);
    CHECK(roundTrip.x == canonical.x && roundTrip.y == canonical.y &&
              roundTrip.width == canonical.width && roundTrip.height == canonical.height,
          "presentation/source mapping round-trips without one-pixel drift");

    const ImageData large = makeImageData(512, 512, PixelFormat::RGB24);
    int checkpoints = 0;
    const auto cancelled = mviewer::core::computeROIChannelStats(
        large, {0, 0, large.width, large.height}, [&]() { return ++checkpoints >= 5; });
    CHECK(cancelled.cancelled && !cancelled.valid && checkpoints == 5,
          "large ROI scan exits cooperatively at a row checkpoint");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    domainAndCoreTests();
    QTemporaryDir temporary;
    CHECK(temporary.isValid(), "temporary directory is available");
    if (!temporary.isValid())
        return 1;
    const QDir dir(temporary.path());
    sourceRegionTruthTests(dir);
    const QString a = writePattern(dir, QStringLiteral("A-long-算法图像.png"), 96, 72, 10);
    const QString b = writePattern(dir, QStringLiteral("B-long-算法图像.png"), 96, 72, 60);

    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *workspace = new CompareWorkspace(&dialog);
    layout->addWidget(workspace);
    workspace->setImages({a, b});
    dialog.resize(1100, 760);
    dialog.show();
    CHECK(waitFor([&]() { return workspace->comparedImageCount() == 2; }),
          "two equal-dimension panes load");

    RawImageView *first = pane(workspace, 0);
    RawImageView *second = pane(workspace, 1);
    CHECK(first && second && first->sourceSize() == QSize(96, 72),
          "real Grid panes expose source geometry");
    if (!first || !second)
        return 1;

    const QPoint gridFrom = first->sourcePointToWidget(QPointF(12, 10)).toPoint();
    const QPoint gridTo = first->sourcePointToWidget(QPointF(40, 32)).toPoint();
    sendRightDrag(first, gridFrom, gridTo, false);
    CHECK(!second->selection().isEmpty() && second->selection().x == first->selection().x &&
              second->selection().y == first->selection().y,
          "Grid live preview reaches the peer before release");
    QMouseEvent gridRelease(QEvent::MouseButtonRelease, QPointF(gridTo), first->mapToGlobal(gridTo),
                            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(first, &gridRelease);
    QTableWidget *table = workspace->findChild<QTableWidget *>("roiMeasurementTable");
    CHECK(table && waitFor([&]() { return table->rowCount() == 2; }),
          "Grid release publishes the real asynchronous measurement table");
    QPushButton *copy = workspace->findChild<QPushButton *>("copyRoiMeasurementsButton");
    CHECK(copy && copy->isEnabled(), "ready ROI table enables Copy ROI Measurements");
    if (copy)
    {
        copy->click();
        CHECK(QApplication::clipboard()->text().contains(QStringLiteral("R Mean\tG Mean")),
              "clipboard export is Excel-ready TSV");
    }
    QPushButton *hud = workspace->findChild<QPushButton *>("roiMeasurementHud");
    CHECK(hud && hud->isVisible() && hud->text().contains(QStringLiteral("Ready")),
          "side-panel-hidden HUD immediately exposes ready results");

    const auto beforePreview = TaskScheduler::instance().metrics(TaskScheduler::AnalysisPool);
    const QPoint rapidStart = first->sourcePointToWidget(QPointF(2, 2)).toPoint();
    QMouseEvent rapidPress(QEvent::MouseButtonPress, QPointF(rapidStart),
                           first->mapToGlobal(rapidStart), Qt::RightButton, Qt::RightButton,
                           Qt::NoModifier);
    QApplication::sendEvent(first, &rapidPress);
    QPoint rapidEnd = rapidStart;
    for (int index = 0; index < 20; ++index)
    {
        rapidEnd = first->sourcePointToWidget(QPointF(20 + index, 15 + index)).toPoint();
        QMouseEvent rapidMove(QEvent::MouseMove, QPointF(rapidEnd), first->mapToGlobal(rapidEnd),
                              Qt::NoButton, Qt::RightButton, Qt::NoModifier);
        QApplication::sendEvent(first, &rapidMove);
    }
    const auto afterPreview = TaskScheduler::instance().metrics(TaskScheduler::AnalysisPool);
    CHECK(afterPreview.submitted == beforePreview.submitted,
          "rapid 20-move ROI preview submits no source scans");
    QMouseEvent rapidRelease(QEvent::MouseButtonRelease, QPointF(rapidEnd),
                             first->mapToGlobal(rapidEnd), Qt::RightButton, Qt::NoButton,
                             Qt::NoModifier);
    QApplication::sendEvent(first, &rapidRelease);
    CHECK(waitFor([&]() { return table->rowCount() == 2; }),
          "rapid redraw release publishes only the final exact ROI");
    const auto afterRelease = TaskScheduler::instance().metrics(TaskScheduler::AnalysisPool);
    CHECK(afterRelease.pending <= 3, "rapid ROI redraw keeps pending Analysis work bounded");

    const auto created = workspace->currentROI();
    const QPoint moveFrom = first
                                ->sourcePointToWidget(QPointF(created.x + created.width / 2.0,
                                                              created.y + created.height / 2.0))
                                .toPoint();
    const QPoint moveTo = first
                              ->sourcePointToWidget(QPointF(created.x + created.width / 2.0 + 5,
                                                            created.y + created.height / 2.0 + 4))
                              .toPoint();
    sendRightDrag(first, moveFrom, moveTo);
    const auto moved = workspace->currentROI();
    CHECK(moved.width == created.width && moved.height == created.height && moved.x > created.x &&
              moved.y > created.y,
          "right-drag inside an existing ROI moves it without changing its size");
    const int fixedRight = moved.x + moved.width;
    const int fixedBottom = moved.y + moved.height;
    const QPoint cornerFrom = first->sourcePointToWidget(QPointF(moved.x, moved.y)).toPoint();
    const QPoint cornerTo = first->sourcePointToWidget(QPointF(moved.x - 4, moved.y - 3)).toPoint();
    sendRightDrag(first, cornerFrom, cornerTo);
    const auto resized = workspace->currentROI();
    CHECK(resized.x < moved.x && resized.y < moved.y && resized.x + resized.width == fixedRight &&
              resized.y + resized.height == fixedBottom,
          "corner resize preserves the opposite source-pixel corner");

    QCheckBox *split = findCheckBox(workspace, QStringLiteral("左右分割"));
    QCheckBox *overlay = findCheckBox(workspace, QStringLiteral("叠加对比"));
    QCheckBox *swipe = findCheckBox(workspace, QStringLiteral("滑动对比"));
    QCheckBox *checker = findCheckBox(workspace, QStringLiteral("棋盘对比"));
    QWidget *canvas = workspace->findChild<QWidget *>("compareCanvas");
    CHECK(split && overlay && swipe && checker && canvas, "all Canvas modes and canvas exist");
    const QList<QCheckBox *> modes = {split, overlay, swipe, checker};
    for (QCheckBox *mode : modes)
    {
        if (!mode || !canvas)
            continue;
        clearROI(workspace);
        mode->setChecked(true);
        pump(30);
        const QPoint from(canvas->width() / 3, canvas->height() / 3);
        const QPoint to(from + QPoint(55, 45));
        const qulonglong renderCount = canvas->property("baseSurfaceRenderCount").toULongLong();
        sendRightDrag(canvas, from, to, false);
        CHECK(
            !workspace->currentROI().isEmpty(),
            qPrintable(QStringLiteral("Canvas live preview creates ROI in %1").arg(mode->text())));
        CHECK(canvas->property("baseSurfaceRenderCount").toULongLong() == renderCount,
              "Canvas ROI preview repaints annotations without re-rasterizing the base surface");
        QMouseEvent release(QEvent::MouseButtonRelease, QPointF(to), canvas->mapToGlobal(to),
                            Qt::RightButton, Qt::NoButton, Qt::NoModifier);
        QApplication::sendEvent(canvas, &release);
        pump(20);
        const auto canonical = workspace->currentROI();
        mode->setChecked(false);
        pump(20);
        CHECK(workspace->currentROI().x == canonical.x &&
                  workspace->currentROI().y == canonical.y &&
                  workspace->currentROI().width == canonical.width &&
                  workspace->currentROI().height == canonical.height,
              "Canvas/Grid switch preserves canonical ROI geometry");
    }

    CHECK(hud && hud->isVisible(), "side-panel-hidden ROI HUD is visible");
    CHECK(copy != nullptr, "ROI table exposes Copy ROI Measurements");
    if (hud)
    {
        hud->click();
        pump(20);
        QCheckBox *side = workspace->findChild<QCheckBox *>("analysisPanelToggle");
        CHECK(side && side->isChecked() && table->isVisible() && !hud->isVisible(),
              "HUD click opens the full visible measurement table");
        if (side)
            side->setChecked(false);
    }

    clearROI(workspace);
    TaskScheduler::instance().pause(TaskScheduler::AnalysisPool);
    sendRightDrag(first, gridFrom, gridTo);
    QLabel *status = workspace->findChild<QLabel *>("roiStatusLabel");
    CHECK(status && status->text().contains(QStringLiteral("Backpressured")),
          "AnalysisPool rejection produces a visible Backpressured terminal state");
    TaskScheduler::instance().resume(TaskScheduler::AnalysisPool);
    sendRightDrag(first, gridFrom, gridTo);
    CHECK(status && waitFor([&]() { return status->text().contains(QStringLiteral("Ready")); }),
          "a new release retries successfully after backpressure clears");

    {
        const QString different =
            writePattern(dir, QStringLiteral("different-size.png"), 80, 60, 90);
        QDialog unequalDialog;
        auto *unequalLayout = new QVBoxLayout(&unequalDialog);
        auto *unequal = new CompareWorkspace(&unequalDialog);
        unequalLayout->addWidget(unequal);
        unequal->setImages({a, different});
        unequalDialog.resize(800, 600);
        unequalDialog.show();
        CHECK(waitFor([&]() { return unequal->comparedImageCount() == 2; }),
              "unequal-dimension pair loads");
        RawImageView *unequalA = pane(unequal, 0);
        RawImageView *unequalB = pane(unequal, 1);
        if (unequalA && unequalB)
        {
            sendRightDrag(unequalA, unequalA->sourcePointToWidget(QPointF(5, 5)).toPoint(),
                          unequalA->sourcePointToWidget(QPointF(30, 25)).toPoint());
            QLabel *unequalStatus = unequal->findChild<QLabel *>("roiStatusLabel");
            CHECK(unequalB->selection().isEmpty() && unequalStatus &&
                      unequalStatus->text().contains(QStringLiteral("image dimensions differ")),
                  "unequal dimensions never fabricate linked measurement");
        }
    }

    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool, std::chrono::seconds(5));

    std::printf("M61 ROI workflow failures: %d\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
