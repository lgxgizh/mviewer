// Space (temporary switch) and the ROI: the held pane shows the other image
// with that image's ROI rectangle and statistics chip, at that image's
// viewport, and Ctrl+C while Space is held copies exactly what is on screen.
// Regression: the chip kept the pane's own numbers and position, and the ROI
// rectangle vanished while Space was held.
#include "compareworkspace.h"
#include "core/scheduler/TaskScheduler.h"
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
#include <QTableWidget>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <functional>

namespace
{
int g_failures = 0;

void check(bool cond, const char *message)
{
    std::printf("%s %s\n", cond ? "[ok]" : "FAIL:", message);
    if (!cond)
        ++g_failures;
}

void pump(int ms = 30)
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
        pump(20);
    return predicate();
}

QString writePattern(const QDir &dir, const QString &name, int bias)
{
    QImage image(96, 72, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
        for (int x = 0; x < image.width(); ++x)
            image.setPixelColor(
                x, y,
                QColor((x * 2 + bias) & 0xff, (y * 3 + bias / 2) & 0xff, (x + y + 40) & 0xff));
    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

RawImageView *pane(CompareWorkspace *workspace, int index)
{
    for (RawImageView *view : workspace->findChildren<RawImageView *>())
        if (view->cellIndex() == index)
            return view;
    return nullptr;
}

QLabel *chip(CompareWorkspace *workspace, int index)
{
    return workspace->findChild<QLabel *>(QStringLiteral("roiPaneChip%1").arg(index));
}

bool chipReady(const QLabel *label)
{
    return label && label->isVisible() && label->text().contains(QStringLiteral("R/G")) &&
           !label->text().contains(QStringLiteral("…"));
}

QCheckBox *findCheckBox(QWidget *root, const QString &prefix)
{
    for (QCheckBox *box : root->findChildren<QCheckBox *>())
        if (box->text().startsWith(prefix))
            return box;
    return nullptr;
}

void rightDrag(QWidget *target, const QPoint &from, const QPoint &to)
{
    QMouseEvent press(QEvent::MouseButtonPress, QPointF(from), target->mapToGlobal(from),
                      Qt::RightButton, Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &press);
    QMouseEvent move(QEvent::MouseMove, QPointF(to), target->mapToGlobal(to), Qt::NoButton,
                     Qt::RightButton, Qt::NoModifier);
    QApplication::sendEvent(target, &move);
    pump(10);
    QMouseEvent up(QEvent::MouseButtonRelease, QPointF(to), target->mapToGlobal(to),
                   Qt::RightButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(target, &up);
    pump(10);
}

void hover(QWidget *view, QPoint local)
{
    QCursor::setPos(view->mapToGlobal(local));
    QEnterEvent enter(QPointF(local), QPointF(local), QPointF(view->mapToGlobal(local)));
    QApplication::sendEvent(view, &enter);
    pump(20);
}

void space(QWidget *ws, bool press)
{
    QKeyEvent key(press ? QEvent::KeyPress : QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(ws, &key);
    pump(80);
}

void ctrlWheel(RawImageView *view, QPointF pos, int steps)
{
    for (int i = 0; i < steps; ++i)
    {
        QWheelEvent wheel(pos, view->mapToGlobal(pos), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                          Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(view, &wheel);
        pump(30);
    }
}

QImage copyView(CompareWorkspace *workspace)
{
    QApplication::clipboard()->clear();
    QKeyEvent copy(QEvent::KeyPress, Qt::Key_C, Qt::ControlModifier);
    QApplication::sendEvent(workspace, &copy);
    return QApplication::clipboard()->image().convertToFormat(QImage::Format_RGB32);
}

// Chip position relative to the widget it annotates.
QPoint chipIn(CompareWorkspace *ws, QLabel *label, QWidget *frame, QPoint frameOrigin = {})
{
    return label->geometry().topLeft() - frame->mapTo(ws, frameOrigin);
}

bool near(QPoint a, QPoint b, int tol = 2)
{
    return std::abs(a.x() - b.x()) <= tol && std::abs(a.y() - b.y()) <= tol;
}

bool isAccent(QRgb c)
{
    return qRed(c) > 220 && qGreen(c) > 180 && qGreen(c) < 240 && qBlue(c) < 110;
}

// The #FFD233 ROI outline is painted along the top edge of `box` (widget px).
bool accentAlongTop(const QImage &img, const QRectF &box, double dpr)
{
    int hits = 0;
    for (int i = 1; i <= 5; ++i)
    {
        const double x = box.left() + box.width() * i / 6.0;
        bool hit = false;
        for (int dy = -2; dy <= 2 && !hit; ++dy)
        {
            const QPoint p(int(x * dpr), int((box.top() + dy) * dpr));
            hit = img.rect().contains(p) && isAccent(img.pixel(p));
        }
        hits += hit ? 1 : 0;
    }
    return hits >= 4;
}

bool sameColor(QRgb a, QRgb b)
{
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
               std::abs(qBlue(a) - qBlue(b)) <
           40;
}

// Fraction of matching samples between two equally sized regions of `img`.
double regionMatch(const QImage &img, const QRect &a, const QRect &b)
{
    int same = 0;
    int total = 0;
    for (int j = 1; j < 12; ++j)
        for (int i = 1; i < 12; ++i)
        {
            const QPoint pa(a.left() + a.width() * i / 12, a.top() + a.height() * j / 12);
            const QPoint pb(b.left() + b.width() * i / 12, b.top() + b.height() * j / 12);
            if (!img.rect().contains(pa) || !img.rect().contains(pb))
                continue;
            ++total;
            same += sameColor(img.pixel(pa), img.pixel(pb)) ? 1 : 0;
        }
    return total ? double(same) / total : 0.0;
}

QRect scaled(const QRect &r, double s)
{
    return QRect(int(r.x() * s), int(r.y() * s), int(r.width() * s), int(r.height() * s));
}

QRectF presentedBox(const RawImageView *view, const mviewer::domain::Selection &sel)
{
    return QRectF(
               view->presentedSourcePointToWidget(QPointF(sel.x, sel.y)),
               view->presentedSourcePointToWidget(QPointF(sel.x + sel.width, sel.y + sel.height)))
        .normalized();
}

void gridHold(CompareWorkspace *ws, QWidget *page, RawImageView *v0, RawImageView *v1,
              const char *label)
{
    std::printf("--- %s ---\n", label);
    QLabel *c0 = chip(ws, 0);
    QLabel *c1 = chip(ws, 1);
    const QString own0 = c0->text();
    const QString own1 = c1->text();
    const QPoint ownPos0 = chipIn(ws, c0, v0);
    const QPoint sourcePos = chipIn(ws, c1, v1);
    check(own0 != own1, "the two images have different ROI statistics");
    const mviewer::domain::Selection sel = ws->currentROI();

    hover(v0, v0->rect().center());
    space(ws, true);
    check(v0->hasTransientDisplay(), "Space over pane A shows image B there");
    check(c0->isVisible() && c0->text() == own1, "pane A's chip shows image B's statistics");
    check(c1->isVisible() && c1->text() == own1, "pane B keeps its own chip");
    std::printf("  chip A at (%d,%d), chip B at (%d,%d)\n", chipIn(ws, c0, v0).x(),
                chipIn(ws, c0, v0).y(), sourcePos.x(), sourcePos.y());
    if (v0->size() == v1->size())
        check(near(chipIn(ws, c0, v0), sourcePos),
              "pane A's chip sits where pane B shows it (same viewport)");
    check(!v0->transientSelection().isEmpty(), "the held image carries its ROI");
    const QRectF heldBox = presentedBox(v0, sel);
    const QRectF sourceBox = presentedBox(v1, sel);
    if (v0->size() == v1->size())
        check(std::abs(heldBox.left() - sourceBox.left()) < 1.5 &&
                  std::abs(heldBox.top() - sourceBox.top()) < 1.5 &&
                  std::abs(heldBox.width() - sourceBox.width()) < 1.5,
              "held ROI maps to the same place as in pane B");
    if (QRectF(v0->rect()).adjusted(3, 3, -3, -3).contains(heldBox))
    {
        const QImage g = v0->grab().toImage();
        check(accentAlongTop(g, heldBox, v0->devicePixelRatioF()),
              "the ROI rectangle is drawn on the held image");
    }
    else
        std::printf("  (held ROI extends past the pane; outline pixel check skipped)\n");

    // Ctrl+C while Space is held copies what is on screen: pane A's area of
    // the copy matches pane B's (same image, viewport, ROI and chip).
    const QImage held = copyView(ws);
    check(!held.isNull(), "Ctrl+C while Space is held puts an image on the clipboard");
    check(v0->hasTransientDisplay(), "Ctrl+C does not end the hold");
    if (!held.isNull() && v0->size() == v1->size())
    {
        const double s = double(held.width()) / page->width();
        const QPoint pageInWs = page->mapTo(ws, QPoint(0, 0));
        const QRect r0 = scaled(QRect(v0->mapTo(ws, QPoint(0, 0)) - pageInWs, v0->size()), s);
        const QRect r1 = scaled(QRect(v1->mapTo(ws, QPoint(0, 0)) - pageInWs, v1->size()), s);
        const double match = regionMatch(held, r0, r1);
        std::printf("  held copy: pane A vs pane B match %.2f\n", match);
        check(match > 0.9, "the held copy shows image B in pane A, as on screen");
        const QRect chipRect = scaled(QRect(c0->mapTo(ws, QPoint(0, 0)) - pageInWs, c0->size()), s);
        int border = 0;
        for (int x = chipRect.left(); x <= chipRect.right(); ++x)
            for (int y = chipRect.top(); y <= chipRect.bottom(); ++y)
                if (held.rect().contains(x, y) && isAccent(held.pixel(x, y)))
                    ++border;
        check(border >= chipRect.width(), "the held copy contains pane A's (followed) chip");
    }

    space(ws, false);
    check(!v0->hasTransientDisplay(), "release restores pane A");
    check(v0->transientSelection().isEmpty(), "release drops the carried ROI");
    check(c0->text() == own0, "release restores pane A's own statistics");
    check(near(chipIn(ws, c0, v0), ownPos0), "release puts pane A's chip back");
}
} // namespace

int main(int argc, char **argv)
{
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    QApplication app(argc, argv);
    QTemporaryDir temporary;
    if (!temporary.isValid())
        return 1;
    const QDir dir(temporary.path());
    const QString a = writePattern(dir, QStringLiteral("space-roi-A.png"), 10);
    const QString b = writePattern(dir, QStringLiteral("space-roi-B.png"), 150);

    QDialog dialog;
    auto *layout = new QVBoxLayout(&dialog);
    auto *ws = new CompareWorkspace(&dialog);
    layout->addWidget(ws);
    ws->setImages({a, b});
    dialog.resize(1100, 760);
    dialog.show();
    check(waitFor([&] { return ws->comparedImageCount() == 2; }), "two panes load");
    const bool ready = waitFor(
        [&]
        {
            RawImageView *v0 = pane(ws, 0);
            RawImageView *v1 = pane(ws, 1);
            return v0 && v1 && v0->sourceSize() == QSize(96, 72) &&
                   v1->sourceSize() == QSize(96, 72) && !v1->displayImage().isNull();
        });
    RawImageView *v0 = pane(ws, 0);
    RawImageView *v1 = pane(ws, 1);
    QWidget *page = ws->findChild<QWidget *>(QStringLiteral("compareGridPage"));
    check(ready && v0 && v1 && page, "grid panes expose source geometry");
    if (!ready || !v0 || !v1 || !page)
        return 1;
    pump(200);

    rightDrag(v0, v0->sourcePointToWidget(QPointF(12, 10)).toPoint(),
              v0->sourcePointToWidget(QPointF(44, 34)).toPoint());
    auto *table = ws->findChild<QTableWidget *>(QStringLiteral("roiMeasurementTable"));
    check(table && waitFor([&] { return table->rowCount() == 2; }), "linked ROI is measured");
    check(waitFor([&] { return chipReady(chip(ws, 0)) && chipReady(chip(ws, 1)); }),
          "both ROI chips show numbers");
    pump(80);
    if (!chip(ws, 0) || !chip(ws, 1))
        return 1;
    gridHold(ws, page, v0, v1, "Grid: Space + ROI at fit");

    // #253: pane B zoomed on its own (Ctrl+wheel); the held view replays that
    // zoom, and the ROI chip must follow the replayed viewport too.
    ctrlWheel(v1, QPointF(v1->width() * 0.35, v1->height() * 0.35), 2);
    pump(400);
    check(waitFor([&] { return chipReady(chip(ws, 1)); }), "chip B follows the local zoom");
    gridHold(ws, page, v0, v1, "Grid: Space + ROI with pane B's local zoom");

    // Split canvas: the hovered half shows the other image with its ROI and
    // chip, and the other half keeps its own image (it used to go blank).
    QCheckBox *split = findCheckBox(ws, QStringLiteral("Split"));
    QWidget *canvas = ws->findChild<QWidget *>(QStringLiteral("compareCanvas"));
    check(split && canvas, "Split mode and canvas exist");
    if (split && canvas)
    {
        std::printf("--- Split: Space + ROI ---\n");
        split->setChecked(true);
        pump(120);
        canvas->repaint();
        const QPoint from(canvas->width() / 8, canvas->height() / 3);
        rightDrag(canvas, from, from + QPoint(60, 45));
        check(!ws->currentROI().isEmpty(), "split ROI exists");
        check(waitFor([&] { return chipReady(chip(ws, 0)) && chipReady(chip(ws, 1)); }),
              "split ROI chips show numbers");
        pump(80);
        QLabel *c0 = chip(ws, 0);
        QLabel *c1 = chip(ws, 1);
        const QString own0 = c0->text();
        const QString own1 = c1->text();
        const int halfW = canvas->width() / 2;
        const QPoint ownPos0 = chipIn(ws, c0, canvas);
        const QPoint sourcePos = chipIn(ws, c1, canvas) - QPoint(halfW, 0);

        hover(canvas, QPoint(canvas->width() / 4, canvas->height() / 2));
        space(ws, true);
        canvas->repaint();
        check(c0->isVisible() && c0->text() == own1, "split: left chip shows image B's numbers");
        check(c1->isVisible() && c1->text() == own1, "split: right chip keeps image B's numbers");
        std::printf("  split chip A at (%d,%d), chip B (half-relative) at (%d,%d)\n",
                    chipIn(ws, c0, canvas).x(), chipIn(ws, c0, canvas).y(), sourcePos.x(),
                    sourcePos.y());
        check(near(chipIn(ws, c0, canvas), sourcePos, 3),
              "split: left chip sits where image B's chip sits in its half");
        const QImage g = canvas->grab().toImage();
        const double s = double(g.width()) / canvas->width();
        const QRect left = scaled(QRect(0, 0, halfW - 2, canvas->height()), s);
        const QRect right = scaled(QRect(halfW + 2, 0, halfW - 2, canvas->height()), s);
        const double match = regionMatch(g, left, right);
        std::printf("  split held halves match %.2f\n", match);
        check(match > 0.85, "split: both halves show image B with its ROI (none blank)");
        const QImage held = copyView(ws);
        check(!held.isNull(), "split: Ctrl+C while Space is held copies the view");

        space(ws, false);
        canvas->repaint();
        check(c0->text() == own0, "split: release restores the left chip");
        check(near(chipIn(ws, c0, canvas), ownPos0), "split: release puts the left chip back");
        split->setChecked(false);
        pump(30);
    }

    TaskScheduler::instance().drain(TaskScheduler::AnalysisPool, std::chrono::seconds(5));
    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
