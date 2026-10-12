// Regression: in Compare at ~200% a pan drag moved the bounded region raster
// with the cursor and the newly exposed strip painted the pane background
// (black) until the mouse release re-rendered the region. RawImageView now
// keeps the latest full-frame raster as a backdrop and paints it under any
// part of the image the region raster does not cover.

#include "widgets/rawimageview.h"

#include <QApplication>
#include <QColor>
#include <QImage>
#include <QPalette>

#include <cstdio>

namespace
{

int g_failures = 0;

void check(bool ok, const char *message)
{
    if (!ok)
    {
        std::printf("FAIL: %s\n", message);
        ++g_failures;
    }
}

QImage solid(const QSize &size, const QColor &color)
{
    QImage image(size, QImage::Format_ARGB32_Premultiplied);
    image.fill(color);
    return image;
}

bool near(QRgb px, const QColor &want)
{
    return qAbs(qRed(px) - want.red()) < 8 && qAbs(qGreen(px) - want.green()) < 8 &&
           qAbs(qBlue(px) - want.blue()) < 8;
}

// Count pane-background pixels inside the exposed strip x in [x0, x1).
int backgroundPixels(const QImage &shot, int x0, int x1)
{
    int count = 0;
    for (int y = 20; y < shot.height() - 20; y += 4)
        for (int x = x0; x < x1; x += 4)
            if (near(shot.pixel(x, y), Qt::black))
                ++count;
    return count;
}

QImage grabView(RawImageView &view)
{
    QImage shot = view.grab().toImage().convertToFormat(QImage::Format_RGB32);
    return shot.scaled(view.size()); // DPR-independent sampling grid
}

// 800x600 source shown at 100% in a 400x300 pane: the region raster covers
// exactly the visible source rect (200,150 400x300), like a sharp zoom LOD.
void showZoomedRegion(RawImageView &view)
{
    const QSize source(800, 600);
    view.setImage(solid(QSize(400, 300), Qt::red), source, QRect(QPoint(0, 0), source));
    view.setImage(solid(QSize(400, 300), Qt::green), source, QRect(200, 150, 400, 300));
    view.setTransform(1.0, QPointF(0, 0));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    RawImageView view;
    QPalette pal = view.palette();
    pal.setColor(QPalette::Dark, Qt::black);
    view.setPalette(pal);
    view.resize(400, 300);
    view.show();
    QApplication::processEvents();

    showZoomedRegion(view);
    check(view.hasBackdrop(), "a full-frame raster is kept as the pan backdrop");
    QImage shot = grabView(view);
    check(near(shot.pixel(200, 150), Qt::green), "the region raster covers the view before pan");

    for (const bool dragging : {true, false})
    {
        view.setDragging(dragging);
        // Pan left by 100 px (all panes dragged left): x in [300,400) is new.
        view.setTransform(1.0, QPointF(-100, 0));
        shot = grabView(view);
        check(near(shot.pixel(100, 150), Qt::green), "the sharp region still paints after pan");
        check(backgroundPixels(shot, 304, 396) == 0,
              "newly exposed strip shows image (backdrop), never the black background");
        check(near(shot.pixel(350, 150), Qt::red), "exposed strip is drawn from the backdrop");
        view.setTransform(1.0, QPointF(0, 0));
    }
    view.setDragging(false);

    // Swapping panes carries each pane's backdrop along with its raster.
    RawImageView other;
    other.resize(400, 300);
    other.swapBackdrop(view);
    check(other.hasBackdrop() && !view.hasBackdrop(), "swapBackdrop moves the stand-in");
    view.swapBackdrop(other);

    // A content change (other source or adjustments) drops the stale stand-in;
    // the strip then honestly shows the background until the next raster. This
    // also proves the strip assertion above detects the original bug.
    view.setBackdropKey(42);
    check(!view.hasBackdrop(), "a new content key drops the backdrop");
    view.setTransform(1.0, QPointF(-100, 0));
    shot = grabView(view);
    check(backgroundPixels(shot, 304, 396) > 0, "without a backdrop the strip is background");

    // A full-frame raster under the same key re-arms it; a source-size change
    // drops it again.
    showZoomedRegion(view);
    check(view.hasBackdrop(), "the next full-frame raster re-arms the backdrop");
    view.setImage(solid(QSize(300, 300), Qt::blue), QSize(900, 900), QRect(0, 0, 300, 300));
    check(!view.hasBackdrop(), "a different source size drops the backdrop");

    std::printf("=== compare pan backdrop tests: %s ===\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
