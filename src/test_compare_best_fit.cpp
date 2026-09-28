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
    auto *viewBar = ws->findChild<QWidget *>(QStringLiteral("compareViewToolbar"));
    check(button && button->text() == QStringLiteral("最适合"), "toolbar button「最适合」");
    check(button && viewBar && viewBar->isAncestorOf(button), "button sits on the view toolbar");
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

    if (g_failures > 0)
    {
        std::printf("compare_best_fit_tests: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("compare_best_fit_tests: PASS\n");
    return 0;
}
