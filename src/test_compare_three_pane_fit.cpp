// Three-pane compare: every pane gets an equal column and the same
// fit-to-window scale. Regression: a long filename without break points
// (camera-style "DSC_..._v3.jpg") made its pane header's minimum width the
// column's minimum, so the short-named left pane was squeezed and its image
// rendered much smaller than the other two.

#include "compareworkspace.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QDialog>
#include <QElapsedTimer>
#include <QImage>
#include <QTemporaryDir>
#include <QVBoxLayout>

#include <algorithm>
#include <cmath>
#include <cstdio>

namespace
{
int g_failures = 0;

void check(bool cond, const char *message)
{
    std::printf(cond ? "[ok] %s\n" : "FAIL: %s\n", message);
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

QString writePng(const QDir &dir, const QString &name, const QColor &color)
{
    const QString path = dir.filePath(name);
    QImage image(1200, 800, QImage::Format_RGB32);
    image.fill(color);
    image.save(path, "PNG");
    return path;
}

RawImageView *paneView(QWidget *root, int pane)
{
    QWidget *cell = root->findChild<QWidget *>(QStringLiteral("comparePane%1").arg(pane));
    return cell ? cell->findChild<RawImageView *>() : nullptr;
}

bool waitForThreePanes(CompareWorkspace *ws, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        bool ready = ws->comparedImageCount() == 3;
        for (int i = 0; ready && i < 3; ++i)
        {
            const RawImageView *view = paneView(ws, i);
            ready = view && view->width() > 32 && view->height() > 32 &&
                    !view->displayImage().isNull() && view->sourceSize().width() > 0;
        }
        if (ready)
            return true;
        pump(25);
    }
    return false;
}

double fitFor(const RawImageView *view)
{
    return std::min(view->width() / 1200.0, view->height() / 800.0);
}

void checkThreePanes(CompareWorkspace *ws, const char *label)
{
    std::printf("-- %s\n", label);
    RawImageView *views[3] = {paneView(ws, 0), paneView(ws, 1), paneView(ws, 2)};
    if (!views[0] || !views[1] || !views[2])
    {
        check(false, "all three pane views exist");
        return;
    }
    const int minW = std::min({views[0]->width(), views[1]->width(), views[2]->width()});
    const int maxW = std::max({views[0]->width(), views[1]->width(), views[2]->width()});
    std::printf("   pane widths %d %d %d\n", views[0]->width(), views[1]->width(),
                views[2]->width());
    check(maxW - minW <= 4, "three panes share the grid width equally");
    double scales[3] = {};
    for (int i = 0; i < 3; ++i)
        scales[i] = ws->engine().cellScale(i);
    std::printf("   cell scales %.4f %.4f %.4f\n", scales[0], scales[1], scales[2]);
    const double hi = std::max({scales[0], scales[1], scales[2]});
    check(hi > 0.0 && scales[0] / hi > 0.97, "left pane is not smaller than the others");
    check(std::abs(scales[1] - scales[2]) / hi < 0.03, "right panes share one fit scale");
    for (int i = 0; i < 3; ++i)
    {
        const double expect = fitFor(views[i]);
        check(expect > 0.0 && std::abs(scales[i] - expect) / expect < 0.03,
              "pane scale is fit-to-window for its own view");
    }
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
    const QString longStem =
        QStringLiteral("DSC_2026_10_13_wedding_ceremony_second_shooter_raw_export_full_resolution");
    const QStringList images = {
        writePng(dir, QStringLiteral("a1.png"), QColor(200, 60, 60)),
        writePng(dir, longStem + QStringLiteral("_v3.png"), QColor(60, 160, 60)),
        writePng(dir, longStem + QStringLiteral("_v4.png"), QColor(60, 60, 200)),
    };

    QDialog host;
    auto *layout = new QVBoxLayout(&host);
    auto *ws = new CompareWorkspace(&host);
    layout->addWidget(ws);
    host.resize(1400, 820);
    host.show();
    pump(50);
    ws->setImages(images);
    check(waitForThreePanes(ws, 20000), "three panes loaded and laid out");
    pump(300);
    checkThreePanes(ws, "long names on the right panes");

    // The window narrows (e.g. restored from maximized): columns stay equal
    // and every pane re-fits instead of the left one collapsing.
    host.resize(1000, 700);
    checkThreePanes(ws, "after narrowing the window");

    // Regression test: snapshot in comparison mode must cover the full canvas area on High-DPI
    // screens without shrinking to 1/DPR or leaving blank dead space when zoomed in.
    const QString snapPath = dir.filePath("test_snap_coverage.png");
    check(ws->saveComparisonViewTo(snapPath), "snapshot saves to file successfully");
    const QImage snapImg(snapPath);
    check(!snapImg.isNull(), "snapshot image is valid");
    const qreal dpr = ws->devicePixelRatioF() > 0.0 ? ws->devicePixelRatioF() : 1.0;
    const int bannerH = static_cast<int>(std::round(36 * dpr));
    check(snapImg.height() > bannerH, "snapshot includes comparison area and banner");

    auto checkCoverage = [&](const QImage &img, const char *label)
    {
        const QRgb bg = qRgb(24, 24, 28);
        int maxX = 0;
        for (int y = 0; y < img.height() - bannerH; y += 8)
        {
            for (int x = img.width() - 1; x > maxX; --x)
            {
                if (img.pixel(x, y) != bg && qAlpha(img.pixel(x, y)) > 0)
                {
                    maxX = x;
                    break;
                }
            }
        }
        check(maxX >= static_cast<int>(img.width() * 0.9), label);
    };

    checkCoverage(snapImg, "snapshot covers full canvas width in fit mode");

    // Zoom in (actual pixels / 100%) and verify that snapshot still covers full canvas width
    ws->zoomActual();
    pump(300);
    const QString snapZoomPath = dir.filePath("test_snap_zoomed.png");
    check(ws->saveComparisonViewTo(snapZoomPath), "zoomed snapshot saves successfully");
    const QImage snapZoomImg(snapZoomPath);
    checkCoverage(snapZoomImg, "snapshot covers full canvas width when zoomed in");

    if (g_failures > 0)
    {
        std::printf("compare_three_pane_fit_tests: FAIL (%d)\n", g_failures);
        return 1;
    }
    std::printf("compare_three_pane_fit_tests: PASS\n");
    return 0;
}
