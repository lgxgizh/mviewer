// Space (temporary switch) shows the other image exactly as its own pane shows
// it, including that pane's independent Ctrl+wheel zoom and pan. Regression:
// 540x720 beside 3072x4096, Ctrl+wheel on the right pane, Space over the left
// pane used to show the right image at plain fit (the zoom was dropped).

#include "compareworkspace.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QEnterEvent>
#include <QImage>
#include <QKeyEvent>
#include <QPainter>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWheelEvent>

#include <algorithm>
#include <cmath>
#include <cstdio>
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

void pump(int ms)
{
    QElapsedTimer timer;
    timer.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (timer.elapsed() < ms);
}

template <typename Pred> bool waitTrue(Pred pred, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < timeoutMs)
        pump(20);
    return pred();
}

// Four flat quadrants split at the image centre, so a sample tells which part
// of the image a pane shows at that spot.
QString writeQuadrants(const QDir &dir, const QString &name, int w, int h, const QColor (&c)[4])
{
    QImage image(w, h, QImage::Format_RGB32);
    QPainter p(&image);
    p.fillRect(0, 0, w / 2, h / 2, c[0]);
    p.fillRect(w / 2, 0, w - w / 2, h / 2, c[1]);
    p.fillRect(0, h / 2, w / 2, h - h / 2, c[2]);
    p.fillRect(w / 2, h / 2, w - w / 2, h - h / 2, c[3]);
    p.end();
    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

RawImageView *paneView(QWidget *root, int pane)
{
    QWidget *cell = root->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(pane));
    return cell ? cell->findChild<RawImageView *>() : nullptr;
}

double fitFor(const RawImageView *view, QSize image)
{
    if (!view || image.isEmpty())
        return 0.0;
    return std::min(static_cast<double>(view->width()) / image.width(),
                    static_cast<double>(view->height()) / image.height());
}

bool closeRel(double a, double b, double tol)
{
    return std::abs(a - b) <= tol * std::max(1.0, std::abs(b));
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

void hover(RawImageView *view)
{
    const QPointF local(view->width() / 2.0, view->height() / 2.0);
    QEnterEvent enter(local, local, view->mapToGlobal(local));
    QApplication::sendEvent(view, &enter);
}

void space(QWidget *ws, bool press)
{
    QKeyEvent key(press ? QEvent::KeyPress : QEvent::KeyRelease, Qt::Key_Space, Qt::NoModifier);
    QApplication::sendEvent(ws, &key);
}

bool sameColor(QRgb a, QRgb b)
{
    return std::abs(qRed(a) - qRed(b)) + std::abs(qGreen(a) - qGreen(b)) +
               std::abs(qBlue(a) - qBlue(b)) <
           60;
}

// Samples the same relative spots of both panes; away from the corners so the
// filename / histogram chips do not decide the outcome.
int matchingSamples(RawImageView *a, RawImageView *b)
{
    const QImage ga = a->grab().toImage();
    const QImage gb = b->grab().toImage();
    static const double spots[][2] = {{0.30, 0.40}, {0.80, 0.40}, {0.30, 0.75},
                                      {0.58, 0.58}, {0.75, 0.75}, {0.45, 0.30}};
    int same = 0;
    for (const auto &s : spots)
    {
        const QPoint pa(static_cast<int>(s[0] * (ga.width() - 1)),
                        static_cast<int>(s[1] * (ga.height() - 1)));
        const QPoint pb(static_cast<int>(s[0] * (gb.width() - 1)),
                        static_cast<int>(s[1] * (gb.height() - 1)));
        if (!ga.isNull() && !gb.isNull() && sameColor(ga.pixel(pa), gb.pixel(pb)))
            ++same;
    }
    return same;
}

struct ViewState
{
    double scale = 0.0;
    QPointF offset;
};

ViewState stateOf(const RawImageView *v)
{
    return {v->scale(), v->offset()};
}

bool sameState(const ViewState &a, const ViewState &b)
{
    return closeRel(a.scale, b.scale, 1e-6) && std::abs(a.offset.x() - b.offset.x()) < 0.5 &&
           std::abs(a.offset.y() - b.offset.y()) < 0.5;
}

// Holds Space over `target`, checks it presents `source` exactly as `source`
// presents it (mapped by fit when the panes differ in size), then releases.
// `whileHeld` runs once the hold is up, before the checks (e.g. waits for a
// raster to land on a pane mid-hold).
void holdAndCheck(QWidget *ws, RawImageView *target, RawImageView *source, const char *label,
                  const std::function<void()> &whileHeld = {})
{
    const ViewState ownTarget = stateOf(target);
    const ViewState ownSource = stateOf(source);
    const QSize image = source->sourceSize();
    const double ratio = fitFor(target, image) / fitFor(source, image);
    hover(target);
    space(ws, true);
    check(waitTrue([&] { return target->hasTransientDisplay(); }, 3000), label);
    if (whileHeld)
        whileHeld();
    pump(150);
    const double shown = target->presentedScale();
    const QPointF shownOffset = target->presentedOffset();
    std::printf("  %s: source scale %.5f offset (%.1f, %.1f); held scale %.5f offset (%.1f, "
                "%.1f); fit ratio %.4f\n",
                label, ownSource.scale, ownSource.offset.x(), ownSource.offset.y(), shown,
                shownOffset.x(), shownOffset.y(), ratio);
    check(closeRel(shown, ownSource.scale * ratio, 1e-3),
          "held pane uses the source pane's zoom (incl. its Ctrl+wheel zoom)");
    check(std::abs(shownOffset.x() - ownSource.offset.x() * ratio) < 1.0 &&
              std::abs(shownOffset.y() - ownSource.offset.y() * ratio) < 1.0,
          "held pane uses the source pane's pan");
    const int same = matchingSamples(target, source);
    std::printf("  matching samples: %d/6\n", same);
    check(same == 6, "held pane shows the same image region as the source pane");
    check(sameState(stateOf(source), ownSource), "source pane view is untouched during the hold");
    space(ws, false);
    check(waitTrue([&] { return !target->hasTransientDisplay(); }, 3000),
          "releasing Space restores the pane");
    check(sameState(stateOf(target), ownTarget), "pane keeps its own zoom and pan after release");
    check(closeRel(target->presentedScale(), ownTarget.scale, 1e-9),
          "presented scale is the pane's own again");
    check(sameState(stateOf(source), ownSource), "other pane is unchanged after release");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    const QDir dir(tmp.path());
    const QColor lowColors[4] = {QColor(230, 40, 40), QColor(40, 200, 40), QColor(40, 60, 230),
                                 QColor(230, 220, 40)};
    const QColor highColors[4] = {QColor(200, 40, 200), QColor(40, 210, 210), QColor(250, 140, 30),
                                  QColor(120, 120, 120)};
    const QString lowPath =
        writeQuadrants(dir, QStringLiteral("low_540x720.png"), 540, 720, lowColors);
    const QString highPath =
        writeQuadrants(dir, QStringLiteral("high_3072x4096.png"), 3072, 4096, highColors);

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    ws->setImages({lowPath, highPath});
    host.resize(1280, 860);
    host.show();
    check(waitTrue([&] { return ws->comparedImageCount() == 2; }, 30000), "both images loaded");
    RawImageView *low = paneView(ws, 0);
    RawImageView *high = paneView(ws, 1);
    if (!low || !high)
    {
        std::printf("FAIL: compare panes not found\n");
        return 1;
    }
    const auto displayed = [&]
    {
        return low->width() > 32 && high->width() > 32 && !low->displayImage().isNull() &&
               !high->displayImage().isNull() && low->sourceSize() == QSize(540, 720) &&
               high->sourceSize() == QSize(3072, 4096);
    };
    check(waitTrue(displayed, 30000), "both panes show their image");
    pump(300);
    std::printf("pane sizes %dx%d / %dx%d\n", low->width(), low->height(), high->width(),
                high->height());

    // Same effective size at fit: both images fill the pane alike.
    check(closeRel(low->scale() * 540, high->scale() * 3072, 0.02),
          "different resolutions present at the same on-screen size at fit");

    // 1) Ctrl+wheel only the right (3072x4096) pane, off-centre, then Space on the left.
    const double lowBefore = low->scale();
    ctrlWheel(high, QPointF(high->width() * 0.3, high->height() * 0.3), 4);
    check(waitTrue([&] { return closeRel(high->scale(), ws->engine().cellScale(1), 1e-6); }, 3000),
          "right pane painted at its local zoom");
    pump(400);
    check(high->scale() / fitFor(high, high->sourceSize()) > 1.5,
          "right pane is zoomed on its own (Ctrl+wheel)");
    check(closeRel(low->scale(), lowBefore, 1e-9), "left pane did not follow the Ctrl+wheel");
    holdAndCheck(ws, low, high, "Space on the left shows the right pane's zoomed view");

    // 2) Reverse: Ctrl+wheel the left pane elsewhere, Space on the right pane.
    ctrlWheel(low, QPointF(low->width() * 0.7, low->height() * 0.65), 3);
    check(waitTrue([&] { return closeRel(low->scale(), ws->engine().cellScale(0), 1e-6); }, 3000),
          "left pane painted at its local zoom");
    pump(400);
    check(low->scale() / fitFor(low, low->sourceSize()) > 1.3, "left pane is zoomed on its own");
    holdAndCheck(ws, high, low, "Space on the right shows the left pane's zoomed view");

    // 3) Linked wheel on top of local zoom: Space still follows the source pane.
    {
        const QPointF c(high->width() / 2.0, high->height() / 2.0);
        QWheelEvent wheel(c, high->mapToGlobal(c), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                          Qt::NoModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(high, &wheel);
        pump(400);
    }
    holdAndCheck(ws, low, high, "linked zoom + local zoom still replays the right pane");

    // 4) A raster landing mid-hold must not drop the held view. Ctrl+wheel the
    //    right (3072x4096) pane and hold Space over it right away: its sharper
    //    LOD raster is requested after an 80 ms debounce, so it lands while the
    //    hold is up. RawImageView::setImage() clears the transient display; the
    //    workspace has to put the held view back (GCC ASan nightly 2026-10-30
    //    caught this as a timing-dependent failure of cases 2 and 3).
    {
        const QPointF at(high->width() * 0.4, high->height() * 0.6);
        QWheelEvent wheel(at, high->mapToGlobal(at), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                          Qt::ControlModifier, Qt::NoScrollPhase, false);
        QApplication::sendEvent(high, &wheel);
        check(waitTrue([&] { return closeRel(high->scale(), ws->engine().cellScale(1), 1e-6); },
                       3000),
              "right pane took the new local zoom");
        const qint64 rasterBefore = high->image().cacheKey();
        holdAndCheck(ws, high, low, "raster landing mid-hold keeps the held view",
                     [&]
                     {
                         const bool landed = waitTrue(
                             [&] { return high->image().cacheKey() != rasterBefore; }, 10000);
                         std::printf("  raster for the held pane %s during the hold\n",
                                     landed ? "landed" : "did not land");
                         check(high->hasTransientDisplay(),
                               "held pane still presents the other image after a raster landed");
                     });
    }

    std::printf("%d failure(s)\n", g_failures);
    return g_failures ? 1 : 0;
}
