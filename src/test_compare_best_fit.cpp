// 「最适合」fits each compare pane to its own resolution (uniform pixel scale off).
// Same aspect, different size — 400×600 beside 800×1200 — must land at about 2×.

#include "compareworkspace.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QCheckBox>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QKeyEvent>
#include <QLabel>
#include <QMouseEvent>
#include <QPushButton>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <cmath>
#include <cstdio>

namespace
{
int g_failures = 0;

void check(bool cond, const char *message)
{
    if (cond)
    {
        std::printf("[ok] %s\n", message);
        return;
    }
    std::printf("FAIL: %s\n", message);
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

void waitForCompareCount(CompareWorkspace *ws, int expected, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (ws->comparedImageCount() != expected && timer.elapsed() < timeoutMs)
        pump(25);
}

QString writePng(const QDir &dir, const QString &name, int w, int h, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage image(w, h, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

double fitScale(int viewW, int viewH, int imageW, int imageH)
{
    const double scaleX = static_cast<double>(viewW) / static_cast<double>(imageW);
    const double scaleY = static_cast<double>(viewH) / static_cast<double>(imageH);
    return scaleX < scaleY ? scaleX : scaleY;
}

RawImageView *paneView(QWidget *root, const char *paneName)
{
    QWidget *pane = root->findChild<QWidget *>(QString::fromLatin1(paneName));
    return pane ? pane->findChild<RawImageView *>() : nullptr;
}

bool waitForPaneSize(CompareWorkspace *ws, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        const RawImageView *low = paneView(ws, "comparePane0");
        const RawImageView *high = paneView(ws, "comparePane1");
        if (low && high && low->width() > 32 && low->height() > 32 && high->width() > 32 &&
            high->height() > 32)
            return true;
        pump(25);
    }
    return false;
}

bool waitForDisplayed(CompareWorkspace *ws, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        const RawImageView *low = paneView(ws, "comparePane0");
        const RawImageView *high = paneView(ws, "comparePane1");
        if (low && high && !low->displayImage().isNull() && !high->displayImage().isNull() &&
            low->sourceSize().width() > 0 && high->sourceSize().width() > 0 &&
            low->sourceSize().height() > 0 && high->sourceSize().height() > 0)
            return true;
        pump(25);
    }
    return false;
}

void holdPress(QWidget *widget)
{
    const QPointF local(10, 10);
    QMouseEvent press(QEvent::MouseButtonPress, local, widget->mapToGlobal(local), Qt::LeftButton,
                      Qt::LeftButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &press);
}

void holdRelease(QWidget *widget)
{
    const QPointF local(10, 10);
    QMouseEvent release(QEvent::MouseButtonRelease, local, widget->mapToGlobal(local),
                        Qt::LeftButton, Qt::NoButton, Qt::NoModifier);
    QApplication::sendEvent(widget, &release);
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
    const QDir dir(tmp.path());
    const QString lowPath =
        writePng(dir, QStringLiteral("low_400x600.png"), 400, 600, QColor(40, 80, 160));
    const QString highPath =
        writePng(dir, QStringLiteral("high_800x1200.png"), 800, 1200, QColor(180, 60, 40));

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    ws->setImages({lowPath, highPath});
    host.resize(1280, 860);
    host.show();
    waitForCompareCount(ws, 2, 20000);
    check(ws->comparedImageCount() == 2, "both images loaded");
    check(waitForPaneSize(ws, 5000), "compare panes have a real size");

    const auto *lowFrame = ws->engine().imageAt(0);
    const auto *highFrame = ws->engine().imageAt(1);
    check(lowFrame && lowFrame->width() == 400 && lowFrame->height() == 600, "pane 0 is 400x600");
    check(highFrame && highFrame->width() == 800 && highFrame->height() == 1200,
          "pane 1 is 800x1200");

    QCheckBox *uniform = nullptr;
    const auto boxes = ws->findChildren<QCheckBox *>();
    for (QCheckBox *box : boxes)
    {
        if (box->text().startsWith(QStringLiteral("统一像素倍率")))
            uniform = box;
    }
    auto *button = ws->findChild<QPushButton *>(QStringLiteral("bestFitButton"));
    auto *toolBar = ws->findChild<QWidget *>(QStringLiteral("compareToolToolbar"));
    check(button && button->text() == QStringLiteral("最适合"), "toolbar button「最适合」");
    check(button && button->isVisible() && toolBar && toolBar->isAncestorOf(button),
          "button sits on the tool actions toolbar");
    check(button && button->toolTip().contains(QStringLiteral("统一像素倍率")),
          "tooltip contrasts FOV fit with uniform pixel scale");
    check(uniform && !uniform->isChecked(), "uniform pixel scale starts off");
    if (!button || !uniform || !lowFrame || !highFrame)
        return 1;

    RawImageView *lowView = paneView(ws, "comparePane0");
    RawImageView *highView = paneView(ws, "comparePane1");
    if (!lowView || !highView)
        return 1;
    std::printf("pane sizes: %dx%d and %dx%d\n", lowView->width(), lowView->height(),
                highView->width(), highView->height());

    uniform->setChecked(true);
    const double uniformLow = ws->engine().cellScale(0);
    const double uniformHigh = ws->engine().cellScale(1);
    const double lowFit = fitScale(lowView->width(), lowView->height(), 400, 600);
    const double highFit = fitScale(highView->width(), highView->height(), 800, 1200);
    const double shared = lowFit < highFit ? lowFit : highFit;
    check(std::abs(uniformLow - uniformHigh) < 1e-9, "uniform pixel scale shares one zoom");
    check(std::abs(uniformLow - shared) < 1e-6, "uniform zoom is the smaller fit scale");

    // F stays Fit: it must not turn uniform pixel scale off.
    QKeyEvent fitKey(QEvent::KeyPress, Qt::Key_F, Qt::NoModifier);
    QApplication::sendEvent(ws, &fitKey);
    check(uniform->isChecked(), "F keeps 统一像素倍率 checked");
    check(std::abs(ws->engine().cellScale(0) - ws->engine().cellScale(1)) < 1e-9,
          "F refits inside uniform pixel scale");

    button->click();
    pump(30);
    check(!uniform->isChecked(), "最适合 turns 统一像素倍率 off");
    const double bestLow = ws->engine().cellScale(0);
    const double bestHigh = ws->engine().cellScale(1);
    std::printf("scales: low=%.6f high=%.6f ratio=%.4f (fits %.6f / %.6f)\n", bestLow, bestHigh,
                bestHigh > 0.0 ? bestLow / bestHigh : 0.0, lowFit, highFit);
    check(bestHigh > 0.0 && std::abs(bestLow - lowFit) < 1e-6, "low-res pane uses its own fit");
    check(std::abs(bestHigh - highFit) < 1e-6, "high-res pane uses its own fit");
    check(std::abs(bestLow / bestHigh - 2.0) < 0.15,
          "low-res scale is about 2x the high-res scale");
    check(std::abs(ws->engine().syncTransform().scale - 1.0) < 1e-9,
          "shared zoom ratio resets to 1");
    check(std::abs(lowView->scale() - bestLow) < 1e-6, "low-res view painted at its fit scale");
    check(std::abs(highView->scale() - bestHigh) < 1e-6, "high-res view painted at its fit scale");

    auto *status = ws->findChild<QLabel *>(QStringLiteral("compareStatusLabel"));
    check(status && status->text() == QStringLiteral("最适合：已按视野对齐并适配窗口"),
          "status toast reports FOV-matched fit");

    check(waitForDisplayed(ws, 20000), "both panes have a display raster");
    button->click();
    pump(40);
    lowView = paneView(ws, "comparePane0");
    highView = paneView(ws, "comparePane1");
    if (!lowView || !highView)
        return 1;

    auto *hold = ws->findChild<QPushButton *>(QStringLiteral("temporaryCompareButton"));
    check(hold && hold->isEnabled(), "temporary switch is available after best fit");
    if (hold && hold->isEnabled())
    {
        const double ownedScale = lowView->scale();
        const double otherScale = highView->scale();
        const double ownedW = ownedScale * lowView->sourceSize().width();
        const double ownedH = ownedScale * lowView->sourceSize().height();
        holdPress(hold);
        pump(30);
        const double presented = lowView->presentedScale();
        const double shownW = presented * highView->sourceSize().width();
        const double shownH = presented * highView->sourceSize().height();
        std::printf(
            "temporary footprint owned %.1fx%.1f shown %.1fx%.1f presented %.4f ownedScale %.4f\n",
            ownedW, ownedH, shownW, shownH, presented, ownedScale);
        check(lowView->hasTransientDisplay(), "hold shows the other image on pane 0");
        check(ownedW > 1.0 && std::abs(shownW - ownedW) / ownedW < 0.08,
              "temporary image keeps the fitted on-screen width");
        check(ownedH > 1.0 && std::abs(shownH - ownedH) / ownedH < 0.08,
              "temporary image keeps the fitted on-screen height");
        check(std::abs(presented - ownedScale) / ownedScale > 0.25,
              "temporary render scale is not the target pane raw scale");
        check(std::abs(presented - 1.0) > 0.05, "temporary scale is not raw 100%");
        check(std::abs(lowView->scale() - ownedScale) < 1e-6, "owned scale stays during the hold");
        check(std::abs(highView->scale() - otherScale) < 1e-6,
              "source pane scale stays during the hold");
        holdRelease(hold);
        pump(30);
        check(!lowView->hasTransientDisplay(), "release restores the pane image");
        check(std::abs(lowView->scale() - ownedScale) < 1e-4, "low pane scale restored after hold");
        check(std::abs(highView->scale() - otherScale) < 1e-4,
              "high pane scale restored after hold");
        check(std::abs(lowView->presentedScale() - lowView->scale()) < 1e-9,
              "presented scale matches the owned scale after the hold");
    }

    // 统一像素倍率 stays pixel-aligned across the same hold.
    uniform->setChecked(true);
    pump(40);
    const double uniformHeld = lowView->scale();
    if (hold && hold->isEnabled())
    {
        holdPress(hold);
        pump(20);
        check(lowView->hasTransientDisplay(), "uniform mode can still hold");
        check(std::abs(lowView->presentedScale() - uniformHeld) < 1e-6,
              "uniform pixel scale is unchanged during temporary switch");
        holdRelease(hold);
        pump(20);
    }
    button->click();
    pump(30);
    check(!uniform->isChecked(), "最适合 again turns 统一像素倍率 off");

    // Tall window: each half pane is width-limited, blink's full-width cell is not.
    host.setMinimumSize(400, 400);
    host.resize(980, 1500);
    pump(120);
    button->click();
    pump(40);
    lowView = paneView(ws, "comparePane0");
    highView = paneView(ws, "comparePane1");
    check(lowView && highView && waitForPaneSize(ws, 3000), "narrow panes have a size");
    if (lowView && highView)
    {
        const double halfLow = ws->engine().cellScale(0);
        const double halfHigh = ws->engine().cellScale(1);
        std::printf("narrow scales %.4f %.4f panes %dx%d %dx%d\n", halfLow, halfHigh,
                    lowView->width(), lowView->height(), highView->width(), highView->height());
        check(halfHigh > 0.0 && std::abs(halfLow / halfHigh - 2.0) < 0.2,
              "narrow layout still FOV-matches");
        auto *blink = ws->findChild<QCheckBox *>(QStringLiteral("blinkCompareToggle"));
        check(blink && blink->isEnabled(), "blink toggle is available");
        if (blink && blink->isEnabled())
        {
            blink->setChecked(true);
            pump(60);
            lowView = paneView(ws, "comparePane0");
            highView = paneView(ws, "comparePane1");
            RawImageView *visible = nullptr;
            int shown = -1;
            if (lowView && lowView->isVisible())
            {
                visible = lowView;
                shown = 0;
            }
            else if (highView && highView->isVisible())
            {
                visible = highView;
                shown = 1;
            }
            check(visible != nullptr, "blink shows one pane");
            if (visible && visible->sourceSize().width() > 0)
            {
                const double expect =
                    fitScale(visible->width(), visible->height(), visible->sourceSize().width(),
                             visible->sourceSize().height());
                const double actual = ws->engine().cellScale(shown);
                const double halfScale = shown == 0 ? halfLow : halfHigh;
                std::printf("blink pane %d %dx%d src %dx%d scale %.4f fit %.4f half %.4f\n", shown,
                            visible->width(), visible->height(), visible->sourceSize().width(),
                            visible->sourceSize().height(), actual, expect, halfScale);
                check(expect > 0.0 && std::abs(actual - expect) / expect < 0.08,
                      "blink keeps the visible pane at its FOV fit");
                check(std::abs(actual - 1.0) > 0.05 || std::abs(expect - 1.0) < 0.05,
                      "blink did not snap the visible pane to 100%");
                check(halfScale > 0.0 && std::abs(expect - halfScale) / halfScale > 0.12,
                      "blink cell is large enough that the fit must change");
                check(std::abs(actual - halfScale) / halfScale > 0.1,
                      "blink refits when the stretched cell changes the fit");
                const int hidden = shown == 0 ? 1 : 0;
                const double hiddenBefore = hidden == 0 ? halfLow : halfHigh;
                const double hiddenNow = ws->engine().cellScale(hidden);
                if (hiddenBefore > 0.0 && std::abs(hiddenBefore - 1.0) > 0.08)
                {
                    check(std::abs(hiddenNow - 1.0) > 0.05,
                          "hidden pane scale did not collapse to 100%");
                }
            }
            blink->setChecked(false);
            pump(150);
            check(waitForPaneSize(ws, 3000), "grid restored after blink");
            const double backLow = ws->engine().cellScale(0);
            const double backHigh = ws->engine().cellScale(1);
            std::printf("after blink scales %.4f %.4f\n", backLow, backHigh);
            check(backHigh > 0.0 && std::abs(backLow / backHigh - 2.0) < 0.2,
                  "leaving blink restores the FOV scale ratio");
            check(std::abs(backLow - 1.0) > 0.05,
                  "leaving blink did not leave the low pane at 100%");
        }
    }

    lowView = paneView(ws, "comparePane0");
    highView = paneView(ws, "comparePane1");
    check(waitForDisplayed(ws, 10000), "rasters back after blink");
    button->click();
    pump(40);
    if (lowView && highView && lowView->sourceSize().width() > 1 && lowView->width() > 32)
    {
        const double before = ws->engine().cellScale(0);
        const double highBefore = ws->engine().cellScale(1);
        const int viewW = lowView->width();
        const int viewH = lowView->height();
        const QSize half(std::max(1, lowView->sourceSize().width() / 2),
                         std::max(1, lowView->sourceSize().height() / 2));
        QImage quarter(half, QImage::Format_RGB32);
        quarter.fill(QColor(20, 200, 40));
        lowView->setImage(quarter, half);
        ws->repaint();
        const double after = ws->engine().cellScale(0);
        const double expect = fitScale(viewW, viewH, half.width(), half.height());
        std::printf("replace before %.4f after %.4f expect %.4f high %.4f\n", before, after, expect,
                    ws->engine().cellScale(1));
        check(expect > 0.0 && std::abs(after - expect) / expect < 0.08,
              "same-aspect replace keeps the FOV fit");
        check(before > 0.0 && std::abs(after / before - 2.0) < 0.15,
              "half linear size about doubles the fit scale");
        check(std::abs(after - 1.0) > 0.05, "replace did not snap to 100%");
        check(std::abs(ws->engine().cellScale(1) - highBefore) < 1e-4,
              "the other pane keeps its fit scale");
        check(std::abs(lowView->scale() - after) < 1e-4, "view scale follows the refit");
    }

    auto *swap = ws->findChild<QPushButton *>(QStringLiteral("compareSwapPanesButton"));
    check(swap && swap->isEnabled(), "swap panes control is available");
    if (auto *syncZoom = ws->findChild<QCheckBox *>(QStringLiteral("syncZoomCheck")))
        syncZoom->setChecked(false);
    const auto dragBoxes = ws->findChildren<QCheckBox *>();
    for (QCheckBox *box : dragBoxes)
    {
        if (box->text().contains(QStringLiteral("同步拖动")))
            box->setChecked(false);
    }
    ws->engine().setCellScale(0, 2.25);
    ws->engine().setCellOffset(0, 10.0, -4.0);
    ws->engine().setCellScale(1, 0.35);
    ws->engine().setCellOffset(1, -6.0, 8.0);
    if (swap && swap->isEnabled())
        swap->click();
    check(std::abs(ws->engine().cellScale(0) - 0.35) < 1e-9, "swap moves pane B scale to A");
    check(std::abs(ws->engine().cellScale(1) - 2.25) < 1e-9, "swap moves pane A scale to B");
    lowView = paneView(ws, "comparePane0");
    highView = paneView(ws, "comparePane1");
    if (lowView && highView)
    {
        check(std::abs(lowView->scale() - 0.35) < 1e-6, "pane A view keeps the swapped scale");
        check(std::abs(highView->scale() - 2.25) < 1e-6, "pane B view keeps the swapped scale");
    }

    if (g_failures > 0)
    {
        std::printf("compare_best_fit_tests: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("compare_best_fit_tests: PASS\n");
    return 0;
}
