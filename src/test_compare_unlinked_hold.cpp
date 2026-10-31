// Unlinked compare regressions:
//  - canvas right-drag keeps the ROI on the pane it was drawn on; a click
//    with no drag only opens the export menu
//  - wheel and pan on the grid during Space leave the hidden pane's engine
//    transform; the canvas wheel still zooms; a held surface follows the
//    on-screen pan
//  - the canvas pixel grid during Space uses the image that is on screen
//  - an unlinked ROI histogram and the copy footer are per pane; a linked
//    session stays one shared rectangle and one footer line

#include "compareworkspace.h"
#include "core/scheduler/TaskScheduler.h"
#include "domain/Selection.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QClipboard>
#include <QCursor>
#include <QDialog>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPainter>
#include <QPushButton>
#include <QTableWidget>
#include <QTemporaryDir>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <functional>

namespace
{
int g_failures = 0;

void check(bool cond, const char *message)
{
    std::printf("%s %s\n", cond ? "[ok]" : "FAIL:", message);
    std::fflush(stdout);
    if (!cond)
        ++g_failures;
}

void phase(const char *name)
{
    std::printf("--- %s ---\n", name);
    std::fflush(stdout);
}

void pump(int ms = 30)
{
    QElapsedTimer timer;
    timer.start();
    do
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

template <typename Pred> bool waitTrue(Pred pred, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < timeoutMs)
        pump(20);
    return pred();
}

QString writeSolid(const QDir &dir, const QString &name, int w, int h, const QColor &color)
{
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

RawImageView *pane(CompareWorkspace *ws, int index)
{
    for (RawImageView *view : ws->findChildren<RawImageView *>())
        if (view->cellIndex() == index)
            return view;
    return nullptr;
}

QCheckBox *findCheckBox(QWidget *root, const QString &prefix)
{
    for (QCheckBox *box : root->findChildren<QCheckBox *>())
        if (box->text().startsWith(prefix))
            return box;
    return nullptr;
}

QString histogramTitle(QWidget *root)
{
    QString found;
    for (QLabel *label : root->findChildren<QLabel *>())
        if (label->text().startsWith(QStringLiteral("Histogram")))
            found = label->text();
    return found;
}

void sendMouse(QWidget *target, QEvent::Type type, const QPoint &pos, Qt::MouseButton button,
               Qt::MouseButtons buttons)
{
    QMouseEvent event(type, QPointF(pos), target->mapToGlobal(pos), button, buttons,
                      Qt::NoModifier);
    QApplication::sendEvent(target, &event);
}

void rightDrag(QWidget *target, const QPoint &from, const QPoint &to)
{
    sendMouse(target, QEvent::MouseButtonPress, from, Qt::RightButton, Qt::RightButton);
    sendMouse(target, QEvent::MouseMove, to, Qt::NoButton, Qt::RightButton);
    pump(10);
    sendMouse(target, QEvent::MouseButtonRelease, to, Qt::RightButton, Qt::NoButton);
    pump(10);
}

// The export menu is posted after the click and then runs QMenu::exec. Arm
// the closer after the click so the nested loop dismisses it.
void rightClickNoDrag(QWidget *target, const QPoint &pos)
{
    sendMouse(target, QEvent::MouseButtonPress, pos, Qt::RightButton, Qt::RightButton);
    sendMouse(target, QEvent::MouseButtonRelease, pos, Qt::RightButton, Qt::NoButton);
    QTimer::singleShot(0,
                       []()
                       {
                           if (QWidget *popup = QApplication::activePopupWidget())
                               popup->close();
                       });
    pump(200);
    if (QWidget *popup = QApplication::activePopupWidget())
        popup->close();
    pump(20);
}

void hover(QWidget *view, const QPoint &local)
{
    QCursor::setPos(view->mapToGlobal(local));
    QEnterEvent enter(QPointF(local), QPointF(local), QPointF(view->mapToGlobal(local)));
    QApplication::sendEvent(view, &enter);
    pump(20);
}

void spaceKey(QWidget *ws, bool press)
{
    QKeyEvent key(press ? QEvent::KeyPress : QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(ws, &key);
    pump(40);
}

void wheelAt(QWidget *target, const QPointF &pos)
{
    QWheelEvent wheel(pos, target->mapToGlobal(pos), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(target, &wheel);
}

struct CellXform
{
    double scale = 0;
    double x = 0;
    double y = 0;
};

CellXform cellXform(CompareWorkspace *ws, int index)
{
    const auto &t = ws->engine().cellTransform(index);
    return {t.scale, t.offset.x, t.offset.y};
}

bool sameXform(const CellXform &a, const CellXform &b)
{
    return std::abs(a.scale - b.scale) < 1e-9 && std::abs(a.x - b.x) < 1e-6 &&
           std::abs(a.y - b.y) < 1e-6;
}

struct Grab
{
    QImage image;
    double sx = 1;
    double sy = 1;
};

Grab grabOf(QWidget *widget)
{
    Grab g;
    g.image = widget->grab().toImage().convertToFormat(QImage::Format_RGB32);
    if (widget->width() > 0 && widget->height() > 0)
    {
        g.sx = static_cast<double>(g.image.width()) / widget->width();
        g.sy = static_cast<double>(g.image.height()) / widget->height();
    }
    return g;
}

QRect mapRect(const Grab &g, const QRect &logical)
{
    return QRect(qRound(logical.x() * g.sx), qRound(logical.y() * g.sy),
                 std::max(1, qRound(logical.width() * g.sx)),
                 std::max(1, qRound(logical.height() * g.sy)))
        .intersected(g.image.rect());
}

template <typename Pred> int countIn(const QImage &image, const QRect &box, Pred pred)
{
    const QRect r = box.intersected(image.rect());
    int n = 0;
    for (int y = r.top(); y < r.bottom(); ++y)
        for (int x = r.left(); x < r.right(); ++x)
            if (pred(image.pixel(x, y)))
                ++n;
    return n;
}

bool solidRed(QRgb c)
{
    return qRed(c) > 160 && qGreen(c) < 80 && qBlue(c) < 80;
}

bool solidGreen(QRgb c)
{
    return qGreen(c) > 140 && qRed(c) < 50 && qBlue(c) < 50;
}

bool solidBlue(QRgb c)
{
    return qBlue(c) > 140 && qRed(c) < 50 && qGreen(c) < 50;
}

// White pen at alpha 72 over (0,180,0) / (0,0,180).
bool gridOnGreen(QRgb c)
{
    return qRed(c) >= 40 && qRed(c) <= 150 && qBlue(c) >= 40 && qBlue(c) <= 150 && qGreen(c) >= 160;
}

bool gridOnBlue(QRgb c)
{
    return qRed(c) >= 40 && qRed(c) <= 150 && qGreen(c) >= 40 && qGreen(c) <= 150 &&
           qBlue(c) >= 160;
}

QString paneRoiLine(QChar tag, const mviewer::domain::Selection &sel)
{
    return QStringLiteral("ROI\t%1\tX=%2\tY=%3\tW=%4\tH=%5\tPixels=%6")
        .arg(tag)
        .arg(sel.x)
        .arg(sel.y)
        .arg(sel.width)
        .arg(sel.height)
        .arg(static_cast<qint64>(sel.width) * sel.height);
}

void unload(QDialog &host, CompareWorkspace *ws)
{
    if (QWidget *popup = QApplication::activePopupWidget())
        popup->close();
    ws->setImages({});
    waitTrue([&] { return ws->comparedImageCount() == 0; }, 10000);
    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool, std::chrono::seconds(5));
    TaskScheduler::instance().drain(TaskScheduler::DecodePool, std::chrono::seconds(5));
    host.hide();
    pump(30);
}

// A Space hold keeps its own pan. Changing the engine offset inside the
// coalesce window must neither slide the cached surface nor rebuild it.
void testHeldSurfaceIgnoresEnginePan()
{
    phase("held surface pan");
    RawImageView view;
    QPalette pal = view.palette();
    pal.setColor(QPalette::Dark, Qt::black);
    view.setPalette(pal);
    view.resize(200, 200);

    QImage image(160, 80, QImage::Format_RGB32);
    QPainter painter(&image);
    painter.fillRect(0, 0, 80, 80, Qt::red);
    painter.fillRect(80, 0, 80, 80, Qt::blue);
    painter.end();

    view.setImage(image, image.size());
    view.setTransform(1.0, QPointF(0, 0));
    view.setTransientDisplay(image, image.size(), QRect(QPoint(0, 0), image.size()), 1.0,
                             QPointF(0, 0));
    view.show();
    pump(30);

    const Grab before = grabOf(&view);
    const auto renders = view.property("baseSurfaceRenderCount").toULongLong();
    // Image is centered: left edge 20, blue starts at x = 100. Sample inside blue.
    const QRect sample(110, 70, 24, 24);
    const int blueBefore = countIn(before.image, mapRect(before, sample), solidBlue);
    const int redBefore = countIn(before.image, mapRect(before, sample), solidRed);
    std::printf("  held sample blue %d red %d renders %llu\n", blueBefore, redBefore,
                static_cast<unsigned long long>(renders));
    check(renders > 0 && blueBefore > redBefore && blueBefore > 200, "held image samples as blue");

    view.setTransform(1.0, QPointF(60, 0));
    const Grab after = grabOf(&view);
    const auto rendersAfter = view.property("baseSurfaceRenderCount").toULongLong();
    const int blueAfter = countIn(after.image, mapRect(after, sample), solidBlue);
    const int redAfter = countIn(after.image, mapRect(after, sample), solidRed);
    std::printf("  after engine pan blue %d red %d renders %llu offset %.1f presented %.1f\n",
                blueAfter, redAfter, static_cast<unsigned long long>(rendersAfter),
                view.offset().x(), view.presentedOffset().x());
    check(rendersAfter == renders, "engine pan does not rebuild the held surface");
    check(blueAfter > redAfter && blueAfter > 200 && redAfter < 40,
          "held image stays put while the engine pan changes");
    check(view.offset().x() > 30.0 && std::abs(view.presentedOffset().x()) < 1e-9,
          "presented pan stays on the held viewport");
    view.hide();
}

bool panesReady(CompareWorkspace *ws, RawImageView *a, RawImageView *b, const QSize &sa,
                const QSize &sb)
{
    return ws->comparedImageCount() == 2 && a && b && a->width() > 32 && b->width() > 32 &&
           !a->displayImage().isNull() && !b->displayImage().isNull() && a->sourceSize() == sa &&
           b->sourceSize() == sb;
}

void testUnlinkedHold(const QDir &dir)
{
    phase("unlinked pair");
    const QString redPath =
        writeSolid(dir, QStringLiteral("a_64x48.png"), 64, 48, QColor(220, 30, 30));
    const QString greenPath =
        writeSolid(dir, QStringLiteral("b_96x40.png"), 96, 40, QColor(30, 180, 40));
    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    ws->setImages({redPath, greenPath});
    host.resize(1280, 860);
    host.show();

    check(waitTrue([&] { return ws->comparedImageCount() == 2; }, 30000), "both images loaded");
    RawImageView *a = pane(ws, 0);
    RawImageView *b = pane(ws, 1);
    check(a && b, "compare panes exist");
    if (!a || !b)
    {
        unload(host, ws);
        return;
    }
    const QSize sizeA(64, 48);
    const QSize sizeB(96, 40);
    check(waitTrue([&] { return panesReady(ws, a, b, sizeA, sizeB); }, 30000),
          "both panes show their image");
    check(!ws->linkedROIAvailable(), "different sizes leave ROI unlinked");
    pump(200);

    phase("grid Space ignores wheel and pan");
    const CellXform fit0 = cellXform(ws, 0);
    const CellXform fit1 = cellXform(ws, 1);
    hover(a, QPoint(a->width() / 2, a->height() / 2));
    spaceKey(ws, true);
    check(a->hasTransientDisplay(), "Space on the grid shows the other pane");
    wheelAt(a, QPointF(a->width() / 2.0, a->height() / 2.0));
    const QPoint panFrom(a->width() / 2, a->height() / 2);
    sendMouse(a, QEvent::MouseButtonPress, panFrom, Qt::LeftButton, Qt::LeftButton);
    sendMouse(a, QEvent::MouseMove, panFrom + QPoint(48, 30), Qt::NoButton, Qt::LeftButton);
    sendMouse(a, QEvent::MouseButtonRelease, panFrom + QPoint(48, 30), Qt::LeftButton,
              Qt::NoButton);
    check(sameXform(cellXform(ws, 0), fit0) && sameXform(cellXform(ws, 1), fit1),
          "wheel and pan during Space leave both engine transforms");
    spaceKey(ws, false);
    check(!a->hasTransientDisplay(), "releasing Space clears the grid hold");
    check(sameXform(cellXform(ws, 0), fit0) && sameXform(cellXform(ws, 1), fit1),
          "releasing Space does not apply the held wheel or pan");

    phase("drag started before Space");
    const CellXform held0 = cellXform(ws, 0);
    const CellXform held1 = cellXform(ws, 1);
    sendMouse(a, QEvent::MouseButtonPress, panFrom, Qt::LeftButton, Qt::LeftButton);
    spaceKey(ws, true);
    sendMouse(a, QEvent::MouseMove, panFrom + QPoint(36, 22), Qt::NoButton, Qt::LeftButton);
    sendMouse(a, QEvent::MouseButtonRelease, panFrom + QPoint(36, 22), Qt::LeftButton,
              Qt::NoButton);
    check(a->hasTransientDisplay() && sameXform(cellXform(ws, 0), held0) &&
              sameXform(cellXform(ws, 1), held1),
          "a drag that started before Space does not pan the hidden pane");
    spaceKey(ws, false);

    phase("wheel outside Space");
    const double rested = cellXform(ws, 0).scale;
    wheelAt(a, QPointF(a->width() / 2.0, a->height() / 2.0));
    pump(30);
    check(cellXform(ws, 0).scale > rested * 1.05, "a wheel outside Space still zooms");

    phase("canvas wheel during Space");
    QCheckBox *split = findCheckBox(ws, QStringLiteral("Split"));
    QWidget *canvas = ws->findChild<QWidget *>(QStringLiteral("compareCanvas"));
    check(split && canvas, "Split mode and canvas exist");
    if (!split || !canvas)
    {
        unload(host, ws);
        return;
    }
    split->setChecked(true);
    check(waitTrue([&] { return canvas->isVisible() && canvas->width() > 80; }, 3000),
          "split canvas is visible");
    canvas->repaint();
    const CellXform beforeCanvas0 = cellXform(ws, 0);
    const CellXform beforeCanvas1 = cellXform(ws, 1);
    const QPoint rightPt(canvas->width() * 3 / 4, canvas->height() / 2);
    hover(canvas, rightPt);
    spaceKey(ws, true);
    canvas->repaint();
    const Grab held = grabOf(canvas);
    const QRect rightSample(rightPt.x() - 24, rightPt.y() - 24, 48, 48);
    const int heldRed = countIn(held.image, mapRect(held, rightSample), solidRed);
    const int heldGreen = countIn(held.image, mapRect(held, rightSample), solidGreen);
    std::printf("  canvas hold sample red %d green %d\n", heldRed, heldGreen);
    check(heldRed > heldGreen && heldRed > 200, "Space on the right half shows pane A's image");
    wheelAt(canvas, QPointF(rightPt));
    pump(20);
    check(cellXform(ws, 0).scale > beforeCanvas0.scale * 1.05 &&
              cellXform(ws, 1).scale > beforeCanvas1.scale * 1.05,
          "canvas wheel during Space still zooms");
    spaceKey(ws, false);

    phase("canvas ROI stays on the drawn pane");
    canvas->repaint();
    const QPoint rightTo = rightPt + QPoint(70, 40);
    rightDrag(canvas, rightPt, rightTo);
    std::printf("  after right drag A %d,%d %dx%d B %d,%d %dx%d current %d,%d %dx%d\n",
                a->selection().x, a->selection().y, a->selection().width, a->selection().height,
                b->selection().x, b->selection().y, b->selection().width, b->selection().height,
                ws->currentROI().x, ws->currentROI().y, ws->currentROI().width,
                ws->currentROI().height);
    check(a->selection().isEmpty() && !b->selection().isEmpty() &&
              ws->currentROI() == b->selection(),
          "right-half drag keeps the ROI on pane B");
    const auto savedB = b->selection();
    const auto savedRoi = ws->currentROI();
    phase("canvas no-drag right-click");
    rightClickNoDrag(canvas, rightPt);
    check(a->selection().isEmpty() && b->selection() == savedB && ws->currentROI() == savedRoi,
          "no-drag right-click leaves the selection where it was");

    const QPoint leftPt(canvas->width() / 4, canvas->height() / 2);
    rightDrag(canvas, leftPt, leftPt + QPoint(64, 36));
    std::printf("  after left drag A %d,%d %dx%d B %d,%d %dx%d\n", a->selection().x,
                a->selection().y, a->selection().width, a->selection().height, b->selection().x,
                b->selection().y, b->selection().width, b->selection().height);
    check(!a->selection().isEmpty() && b->selection() == savedB &&
              ws->currentROI() == a->selection(),
          "left-half drag keeps pane B's box and selects pane A");

    auto *table = ws->findChild<QTableWidget *>(QStringLiteral("roiMeasurementTable"));
    auto *copy = ws->findChild<QPushButton *>(QStringLiteral("copyRoiMeasurementsButton"));
    auto *hist = ws->findChild<HistogramWidget *>(QStringLiteral("analysisHistogram"));
    auto *roiToggle = ws->findChild<QCheckBox *>(QStringLiteral("roiHistogramToggle"));
    check(table && copy && hist && roiToggle, "ROI table, copy, and histogram exist");
    if (!table || !copy || !hist || !roiToggle)
    {
        unload(host, ws);
        return;
    }
    check(waitTrue([&] { return table->rowCount() == 2; }, 8000), "ROI table has both panes");
    QApplication::clipboard()->clear();
    copy->click();
    const QString pasted = QApplication::clipboard()->text();
    const QString lineA = paneRoiLine(QChar('A'), a->selection());
    const QString lineB = paneRoiLine(QChar('B'), b->selection());
    std::printf("  copy footer:\n%s\n", qPrintable(pasted));
    check(pasted.contains(lineA) && pasted.contains(lineB),
          "unlinked copy footer has one ROI line per pane");
    check(!pasted.contains(QStringLiteral("ROI\tX=")), "unlinked copy has no shared ROI line");

    phase("per-pane ROI histogram");
    const qint64 areaA = a->selection().area();
    const qint64 areaB = b->selection().area();
    roiToggle->setChecked(true);
    check(waitTrue(
              [&]
              {
                  return hist->histogramCount() >= 2 && hist->histogramTotal(0) == areaA &&
                         hist->histogramTotal(1) == areaB &&
                         histogramTitle(ws).contains(QStringLiteral("per-pane"));
              },
              8000),
          "unlinked histogram counts each pane's own ROI");

    b->setSelection({});
    roiToggle->setChecked(false);
    pump(30);
    roiToggle->setChecked(true);
    const qint64 fullB = static_cast<qint64>(sizeB.width()) * sizeB.height();
    check(waitTrue(
              [&]
              {
                  return hist->histogramCount() >= 2 && hist->histogramTotal(0) == areaA &&
                         hist->histogramTotal(1) == fullB;
              },
              8000),
          "an empty pane histogram counts that whole image");

    unload(host, ws);
}

void testLinkedFooter(const QDir &dir)
{
    phase("linked pair");
    const QString pathA = writeSolid(dir, QStringLiteral("a_48.png"), 48, 48, QColor(200, 40, 40));
    const QString pathB = writeSolid(dir, QStringLiteral("b_48.png"), 48, 48, QColor(40, 40, 200));
    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    ws->setImages({pathA, pathB});
    host.resize(1100, 760);
    host.show();
    check(waitTrue([&] { return ws->comparedImageCount() == 2; }, 30000), "linked images loaded");
    RawImageView *a = pane(ws, 0);
    RawImageView *b = pane(ws, 1);
    const QSize size(48, 48);
    check(a && b && waitTrue([&] { return panesReady(ws, a, b, size, size); }, 30000),
          "linked panes show their image");
    if (!a || !b)
    {
        unload(host, ws);
        return;
    }
    check(ws->linkedROIAvailable(), "equal sizes link ROI");
    QCheckBox *split = findCheckBox(ws, QStringLiteral("Split"));
    QWidget *canvas = ws->findChild<QWidget *>(QStringLiteral("compareCanvas"));
    if (split)
        split->setChecked(true);
    check(canvas && waitTrue([&] { return canvas->isVisible() && canvas->width() > 80; }, 3000),
          "linked split canvas is visible");
    if (!canvas)
    {
        unload(host, ws);
        return;
    }
    const QPoint from(canvas->width() / 3, canvas->height() / 3);
    rightDrag(canvas, from, from + QPoint(80, 50));
    check(!a->selection().isEmpty() && a->selection() == b->selection() &&
              ws->currentROI() == a->selection(),
          "linked canvas drag shares one ROI");

    auto *table = ws->findChild<QTableWidget *>(QStringLiteral("roiMeasurementTable"));
    auto *copy = ws->findChild<QPushButton *>(QStringLiteral("copyRoiMeasurementsButton"));
    auto *hist = ws->findChild<HistogramWidget *>(QStringLiteral("analysisHistogram"));
    auto *roiToggle = ws->findChild<QCheckBox *>(QStringLiteral("roiHistogramToggle"));
    check(table && copy && hist && roiToggle, "linked ROI controls exist");
    if (!table || !copy || !hist || !roiToggle)
    {
        unload(host, ws);
        return;
    }
    check(waitTrue([&] { return table->rowCount() == 2; }, 8000), "linked ROI table is filled");
    QApplication::clipboard()->clear();
    copy->click();
    const QString pasted = QApplication::clipboard()->text();
    int shared = 0;
    int named = 0;
    for (const QString &line : pasted.split(QLatin1Char('\n')))
    {
        if (line.startsWith(QStringLiteral("ROI\tX=")))
            ++shared;
        if (line.startsWith(QStringLiteral("ROI\tA\t")) ||
            line.startsWith(QStringLiteral("ROI\tB\t")))
            ++named;
    }
    const QString sharedLine = QStringLiteral("ROI\tX=%1\tY=%2\tW=%3\tH=%4\tPixels=%5")
                                   .arg(ws->currentROI().x)
                                   .arg(ws->currentROI().y)
                                   .arg(ws->currentROI().width)
                                   .arg(ws->currentROI().height)
                                   .arg(ws->currentROI().area());
    check(shared == 1 && named == 0 && pasted.contains(sharedLine),
          "linked copy footer is one shared ROI line");

    const qint64 area = ws->currentROI().area();
    roiToggle->setChecked(true);
    check(waitTrue(
              [&]
              {
                  return hist->histogramCount() >= 2 && hist->histogramTotal(0) == area &&
                         hist->histogramTotal(1) == area &&
                         histogramTitle(ws).contains(QStringLiteral("Histogram (ROI ")) &&
                         !histogramTitle(ws).contains(QStringLiteral("per-pane"));
              },
              8000),
          "linked histogram uses the shared ROI");
    unload(host, ws);
}

void testCanvasPixelGrid(const QDir &dir)
{
    phase("canvas pixel grid during Space");
    const QString bluePath =
        writeSolid(dir, QStringLiteral("wide_blue.png"), 480, 360, QColor(0, 0, 180));
    const QString greenPath =
        writeSolid(dir, QStringLiteral("tiny_green.png"), 16, 16, QColor(0, 180, 0));
    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    ws->setImages({bluePath, greenPath});
    host.resize(1400, 900);
    host.show();
    check(waitTrue([&] { return ws->comparedImageCount() == 2; }, 30000), "grid images loaded");
    RawImageView *blue = pane(ws, 0);
    RawImageView *green = pane(ws, 1);
    const QSize blueSize(480, 360);
    const QSize greenSize(16, 16);
    check(blue && green &&
              waitTrue([&] { return panesReady(ws, blue, green, blueSize, greenSize); }, 30000),
          "grid panes show their image");
    if (!blue || !green)
    {
        unload(host, ws);
        return;
    }
    check(waitTrue(
              [&]
              {
                  return !blue->softLoading() && !green->softLoading() &&
                         blue->image().width() + 1 >= blueSize.width() &&
                         green->image().width() + 1 >= greenSize.width();
              },
              15000),
          "pixel grid rasters match the source");

    if (QCheckBox *names =
            ws->findChild<QCheckBox *>(QStringLiteral("compareFilenameOverlayToggle")))
        names->setChecked(false);
    ws->setPixelGridEnabled(true);
    QCheckBox *split = findCheckBox(ws, QStringLiteral("Split"));
    QWidget *canvas = ws->findChild<QWidget *>(QStringLiteral("compareCanvas"));
    check(split && canvas, "pixel-grid canvas exists");
    if (!split || !canvas)
    {
        unload(host, ws);
        return;
    }
    split->setChecked(true);
    check(waitTrue([&] { return canvas->isVisible() && canvas->width() > 200; }, 3000),
          "pixel-grid split canvas is visible");
    std::printf("  canvas %dx%d\n", canvas->width(), canvas->height());

    const int mid = canvas->width() / 2;
    const QPoint center(mid + (canvas->width() - mid) / 2, canvas->height() / 2);
    const QRect sample(center.x() - 40, center.y() - 40, 80, 80);
    int greenGrid = 0;
    int greenSolid = 0;
    check(waitTrue(
              [&]
              {
                  canvas->repaint();
                  const Grab g = grabOf(canvas);
                  const QRect box = mapRect(g, sample);
                  greenGrid = countIn(g.image, box, gridOnGreen);
                  greenSolid = countIn(g.image, box, solidGreen);
                  return greenGrid > 15 && greenSolid > 80;
              },
              8000),
          "right half draws the small image's pixel grid");
    std::printf("  before Space grid-on-green %d solid-green %d\n", greenGrid, greenSolid);

    hover(canvas, center);
    spaceKey(ws, true);
    canvas->repaint();
    const Grab held = grabOf(canvas);
    const QRect heldBox = mapRect(held, sample);
    const int blueGrid = countIn(held.image, heldBox, gridOnBlue);
    const int blueSolid = countIn(held.image, heldBox, solidBlue);
    const int greenLeft = countIn(held.image, heldBox, solidGreen);
    std::printf("  during Space grid-on-blue %d solid-blue %d solid-green %d\n", blueGrid,
                blueSolid, greenLeft);
    check(blueSolid > greenLeft && blueSolid > 80, "Space shows the large image on the right half");
    check(blueGrid < 8, "pixel grid follows the shown image and stays off below 800%");
    spaceKey(ws, false);
    unload(host, ws);
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testHeldSurfaceIgnoresEnginePan();

    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    const QDir dir(tmp.path());
    testUnlinkedHold(dir);
    testLinkedFooter(dir);
    testCanvasPixelGrid(dir);

    std::printf("=== compare unlinked hold tests: %s (%d) ===\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
