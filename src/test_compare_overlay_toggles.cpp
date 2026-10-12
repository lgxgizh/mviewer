// Compare overlay toggles must erase their drawing from every pane the moment
// they are switched off (shortcut, checkbox or mirrored menu action alike):
//  - 同步准星 (R): the synced crosshair used to stay on screen after R off.
//  - 显示差异: switching it off during a zoom/pan (deferred heavy work) used to
//    keep the heatmap until the interaction settled.

#include "compareworkspace.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QMouseEvent>
#include <QTemporaryDir>
#include <QVBoxLayout>
#include <QWheelEvent>

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
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    while (timer.elapsed() < ms);
}

bool waitTrue(const std::function<bool()> &pred, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (!pred() && timer.elapsed() < timeoutMs)
        pump(25);
    return pred();
}

QString writePng(const QDir &dir, const QString &name, const QColor &color, bool stripe)
{
    QImage image(320, 240, QImage::Format_RGB32);
    image.fill(color);
    if (stripe)
        for (int y = 100; y < 140; ++y)
            for (int x = 0; x < 320; ++x)
                image.setPixel(x, y, qRgb(255, 255, 255));
    const QString path = dir.filePath(name);
    image.save(path, "PNG");
    return path;
}

RawImageView *paneView(QWidget *root, int idx)
{
    QWidget *pane = root->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(idx));
    return pane ? pane->findChild<RawImageView *>() : nullptr;
}

void hover(RawImageView *view)
{
    const QPointF local(view->width() / 2.0, view->height() / 2.0);
    QMouseEvent move(QEvent::MouseMove, local, view->mapToGlobal(local), Qt::NoButton, Qt::NoButton,
                     Qt::NoModifier);
    QApplication::sendEvent(view, &move);
}

bool anyCrosshair(QWidget *root)
{
    for (int i = 0; i < 2; ++i)
        if (RawImageView *v = paneView(root, i); v && v->hasCrosshair())
            return true;
    return false;
}

bool allCrosshair(QWidget *root)
{
    for (int i = 0; i < 2; ++i)
        if (RawImageView *v = paneView(root, i); !v || !v->hasCrosshair())
            return false;
    return true;
}

bool anyOverlay(QWidget *root)
{
    for (int i = 0; i < 2; ++i)
        if (RawImageView *v = paneView(root, i); v && !v->overlay().isNull())
            return true;
    return false;
}

void pressKey(QWidget *target, Qt::Key key)
{
    QKeyEvent ev(QEvent::KeyPress, key, Qt::NoModifier);
    QApplication::sendEvent(target, &ev);
}

void crosshairCases(CompareWorkspace *ws, RawImageView *v0)
{
    auto *chk = ws->findChild<QCheckBox *>(QStringLiteral("syncCrosshairToggle"));
    check(chk && !chk->isChecked(), "sync crosshair starts off");
    if (!chk)
        return;

    pressKey(ws, Qt::Key_R);
    check(chk->isChecked(), "R turns sync crosshair on");
    hover(v0);
    check(allCrosshair(ws), "hover draws the synced crosshair on every pane");
    pressKey(ws, Qt::Key_R);
    check(!chk->isChecked(), "R again turns sync crosshair off");
    check(!anyCrosshair(ws), "R off clears the crosshair from every pane immediately");

    chk->setChecked(true);
    hover(v0);
    check(allCrosshair(ws), "checkbox on + hover draws the crosshair");
    chk->setChecked(false); // also the path of the mirrored menu action
    check(!anyCrosshair(ws), "checkbox/menu off clears the crosshair immediately");
}

void diffOverlayCases(CompareWorkspace *ws, RawImageView *v0)
{
    auto *chk = ws->findChild<QCheckBox *>(QStringLiteral("diffOverlayToggle"));
    check(chk != nullptr, "diff overlay toggle exists");
    if (!chk)
        return;
    check(waitTrue([chk] { return chk->isEnabled(); }, 20000), "diff overlay toggle enabled");
    chk->setChecked(true);
    check(waitTrue([ws] { return anyOverlay(ws); }, 20000), "diff heatmap is drawn when on");

    // Zoom marks the workspace busy, so heavy diff work is deferred. Turning
    // the heatmap off right now must still clear it at once.
    const QPointF c(v0->width() / 2.0, v0->height() / 2.0);
    QWheelEvent wheel(c, v0->mapToGlobal(c), QPoint(0, 0), QPoint(0, 120), Qt::NoButton,
                      Qt::NoModifier, Qt::NoScrollPhase, false);
    QApplication::sendEvent(v0, &wheel);
    chk->setChecked(false);
    check(!anyOverlay(ws), "diff off during zoom clears the heatmap immediately");
    pump(1500);
    check(!anyOverlay(ws), "no stale heatmap reappears after the interaction settles");
}
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid())
        return 1;
    const QDir dir(tmp.path());
    const QString a = writePng(dir, QStringLiteral("a.png"), QColor(40, 80, 160), false);
    const QString b = writePng(dir, QStringLiteral("b.png"), QColor(40, 80, 160), true);

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    ws->setImages({a, b});
    host.resize(1200, 800);
    host.show();
    check(waitTrue([ws] { return ws->comparedImageCount() == 2; }, 20000), "both images load");
    check(waitTrue(
              [ws]
              {
                  RawImageView *v = paneView(ws, 0);
                  return v && !v->displayImage().isNull() && v->width() > 32;
              },
              20000),
          "pane 0 displays");
    RawImageView *v0 = paneView(ws, 0);
    if (!v0)
        return 1;

    crosshairCases(ws, v0);
    diffOverlayCases(ws, v0);

    std::printf("=== compare overlay toggle tests: %s ===\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
