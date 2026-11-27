// Review P2-09: the GPU compositing branch of paintCompareCanvas().
//
// The panes are rasterised with the SAME calls the CPU path uses, into one QImage
// per pane, so their pixels are identical by construction. What this file moves
// into a fragment shader is only the decision "which pixels of A and B land
// where". A 1:1 texel read is exact, so split / swipe / overlay / checkerboard
// reproduce the old composite bit for bit - verified against
// tests/gl_baseline on real hardware.
#include "compareworkspace_p.h"

#include "compareworkspace_composite_gl.h"

#include <QImage>
#include <QPainter>
#include <QOpenGLContext>

QImage CompareWorkspace::compositeCanvasWithGl(const QByteArray &cacheKey, const QRect &canvasRect,
                                               const QSize &physical, qreal dpr)
{
    if (!m_compositeGl)
        m_compositeGl = std::make_unique<mviewer::ui::CompareCompositeGL>();

    const bool split = d->splitChk && d->splitChk->isChecked();
    const bool swipe = m_swipeChk && m_swipeChk->isChecked();
    const bool overlay = d->overlayChk && d->overlayChk->isChecked();
    const bool checker = d->checkerChk && d->checkerChk->isChecked();
    if ((!split && !swipe && !overlay && !checker) || m_canvasTemporaryPane >= 0)
        return QImage(); // no canvas mode, or a held pane: keep the CPU path

    // Checkerboard stays on the CPU path. QPainter anti-aliases the QRegion clip
    // at every block edge (a boundary pixel comes out as a ~50/50 blend of both
    // panes); reproducing that analytically would mean copying QPainter's scan
    // converter, and a hard switch is measurably wrong - see
    // tests/gl_baseline/README.md. Split / swipe / overlay composite exact.
    if (checker)
        return QImage();

    m_compositeGl->ensureContext(QOpenGLContext::currentContext());
    if (!m_compositeGl->isReady())
        return QImage();

    const int logicalW = canvasRect.width();
    const int logicalH = canvasRect.height();
    if (logicalW <= 0 || logicalH <= 0)
        return QImage();

    // Each pane is rasterised exactly as the CPU path would draw it, using that
    // pane's own geometry (its half in Split, the full canvas otherwise).
    auto rasterPane = [&](int slot) -> QImage
    {
        QImage img(physical, QImage::Format_ARGB32_Premultiplied);
        img.setDevicePixelRatio(dpr);
        // Same background the CPU base surface starts from, so the composite can
        // stay fully opaque (a texture read is exact, a blended one would not be).
        img.fill(palette().color(QPalette::Dark));
        QPainter p(&img);
        p.setRenderHint(QPainter::SmoothPixmapTransform, true);
        const QRectF geom = canvasPaneGeometry(slot);
        drawCellCompare(p, displayedImageIndex(slot), canvasRect, geom);
        p.end();
        return img;
    };
    mviewer::ui::CompareCompositeMode mode = mviewer::ui::CompareCompositeMode::Split;
    if (overlay)
        mode = mviewer::ui::CompareCompositeMode::Overlay;

    // Divider and checker sizes are logical in the CPU path; the texture is
    // physical, so scale them. Split's boundary is the pane geometry (the
    // contract splits at the midpoint), NOT m_splitPos - that is Swipe only.
    const int dividerX =
        split ? qRound(canvasPaneGeometry(1).toRect().left() * dpr)
              : qRound(canvasRect.width() * m_splitPos * dpr);
    const int checkerEdge = std::max(4, m_checkerSize) * qRound(dpr);

    QImage out = m_compositeGl->compositeCached(
        cacheKey,
        [&](QImage &a, QImage &b)
        {
            a = rasterPane(0);
            b = rasterPane(1);
        },
        mode, physical, dividerX, std::clamp(m_overlayAlpha / 100.0, 0.0, 1.0), checkerEdge);
    if (!out.isNull())
        out.setDevicePixelRatio(dpr); // physical raster: paint it 1:1, not scaled
    return out;
}
