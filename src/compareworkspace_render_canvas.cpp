#include "compareworkspace_p.h"

#include <algorithm>
void CompareWorkspace::toggleBlink()
{
    m_blinkState = !m_blinkState;
    applyBlink(m_blinkState);
    update();
}

// M15 P0#1: start the blink timer at the given interval (ms). Extracted so the
// persisted blink interval can be restored via applySession().
void CompareWorkspace::startBlink(int intervalMs)
{
    if (!m_blinkTimer)
    {
        m_blinkTimer = new QTimer(this);
        connect(m_blinkTimer, &QTimer::timeout, this, [this]() { this->toggleBlink(); });
    }
    m_blinkTimer->start(intervalMs);
    m_blinkState = false;
    applyBlink(m_blinkState);
    syncEngineBlink();
}

void CompareWorkspace::stopBlink()
{
    if (m_blinkTimer)
        m_blinkTimer->stop();
    m_engine.clearBlink(); // M24: capture must report "blink off" after stopping

    // Restore visibility before rebuilding the normal grid. rebuildCells()
    // centrally reattaches any pane detached by two-image Blink.
    for (auto *v : m_cellViews)
    {
        if (!v)
            continue;
        QWidget *pane = v->parentWidget();
        if (!pane)
            continue;
        pane->setVisible(true);
    }
    // Rebuild the grid layout to restore proper cell positions after blink
    // may have repositioned cells.
    rebuildCells();
    schedulePostLayoutFit();
    update();
}

// M24: drive the engine's BlinkController from the workspace blink state. The
// engine is the single source for CompareEngine::session(), so without this the
// persisted CompareSession never carried the blink target (always -1).
void CompareWorkspace::syncEngineBlink()
{
    const int n = m_engine.imageCount();
    if (n >= 2)
    {
        const int base = qBound(0, diffBaseIndex(), n - 1);
        m_engine.setBlinkIndex(base == 0 ? 1 : 0);
    }
    else
    {
        m_engine.clearBlink();
    }
}

void CompareWorkspace::applyBlink(bool state)
{
    const int n = m_cellViews.size();
    if (n == 0)
        return;

    // M1: honor the locked reference as the blink base. diffBaseIndex() returns 0
    // when nothing is locked, so this is a strict superset of the old behavior and
    // keeps blink consistent with the diff/inspector (which also use diffBaseIndex).
    const int base = qBound(0, diffBaseIndex(), n - 1);

    // For exactly two images, blink looks best when the active image fills the
    // entire grid area (rather than staying in its own cell slot). We achieve
    // this by showing only the active cell and stretching it across the grid.
    if (n == 2 && m_grid && m_layout)
    {
        const int other = (base == 0) ? 1 : 0;
        const int activeIdx = state ? other : base;
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            QWidget *pane = m_cellViews[i]->parentWidget();
            if (pane)
                pane->setVisible(i == activeIdx);
        }
        // Reposition the active cell to span the entire grid area.
        for (int i = 0; i < m_layout->count(); ++i)
        {
            QLayoutItem *item = m_layout->itemAt(i);
            if (item && item->widget())
            {
                m_layout->removeWidget(item->widget());
                --i;
            }
        }
        // Re-add the active cell's parent widget spanning the full grid.
        if (activeIdx < m_cellViews.size() && m_cellViews[activeIdx])
        {
            auto *cellWidget = m_cellViews[activeIdx]->parentWidget();
            if (cellWidget)
            {
                cellWidget->setVisible(true);
                m_layout->addWidget(cellWidget, 0, 0, -1, -1);
            }
        }
        QTimer::singleShot(0, this, &CompareWorkspace::positionCellHists);
    }
    else
    {
        // For 3+ images: toggle visibility of cell 0 vs all others.
        for (int i = 0; i < n; ++i)
        {
            if (!m_cellViews[i])
                continue;
            QWidget *pane = m_cellViews[i]->parentWidget();
            if (!pane)
                continue;
            if (i == base)
                pane->setVisible(!state);
            else
                pane->setVisible(state);
        }
    }
}

void CompareWorkspace::paintEvent(QPaintEvent *)
{
    // Cells are raw QWidgets (RawImageView) that paint themselves. The workspace
    // only pushes the synchronized transform (scale + offset) so every cell tracks
    // the shared zoom/pan state. Image decode/overlay/draw lives in RawImageView,
    // never in this compare layer (see AGENTS.md: no decode in the QWidget layer).
    const int n = m_engine.imageCount();
    for (int i = 0; i < n; ++i)
    {
        if (i >= m_cellViews.size() || !m_cellViews[i])
            continue;
        const auto &ct = m_engine.cellTransform(i);
        const double sc = ct.scale;
        const QPointF off = m_syncDrag ? QPointF(m_engine.syncTransform().offset.x,
                                                 m_engine.syncTransform().offset.y)
                                       : QPointF(ct.offset.x, ct.offset.y);
        m_cellViews[i]->setTransform(sc, off);
    }

    // M34: canvas modes paint on the dedicated compareCanvas widget. Forward any
    // parent repaint to the canvas so engine transform / overlay changes that
    // only call update() on the parent still refresh the visible canvas page.
    if (m_compareCanvas && n == 2 && anyCanvasCompareMode())
        m_compareCanvas->update();

    // A-4.3: draw connecting lines between linked points across cells (2-up).
    if (m_pixelLinkChk && m_pixelLinkChk->isChecked() && !m_linkPoints.isEmpty() && n >= 2)
    {
        QPainter p(this);
        drawPixelLinkLines(p);
    }
}

void CompareWorkspace::drawCellCompare(QPainter &p, int idx, const QRect &clipRect,
                                       const QRectF &geomRect)
{
    if (idx < 0 || idx >= m_cellViews.size() || !m_cellViews[idx])
        return;
    const QImage &img = m_cellViews[idx]->image();
    if (img.isNull() || clipRect.isEmpty() || geomRect.isEmpty())
        return;
    const QRectF dr = cellDestRect(idx, geomRect);
    if (dr.isEmpty())
        return;

    // H3: render with the SAME synchronized transform as Normal mode (scale +
    // offset) so the user's zoom/pan carries over into split/swipe/overlay.
    // The offset is a pan delta from the target rect's center (center-relative),
    // exactly like RawImageView stores it for a cell widget.
    p.save();
    p.setClipRect(clipRect);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(dr, img);

    // Diff/heatmap overlay (set per cell by the async batch result). Only the
    // non-base cell carries one, so this is a no-op for the base image —
    // preserving the diff view the engineer had in Normal mode.
    const QImage &ov = m_cellViews[idx]->overlay();
    if (!ov.isNull())
    {
        p.setOpacity(m_cellViews[idx]->overlayOpacity());
        p.drawImage(cellFullDestRect(idx, geomRect), ov);
        p.setOpacity(1.0);
    }
    p.restore();
}

void CompareWorkspace::drawSplitCompare(QPainter &p)
{
    const QRect r = canvasRect();
    const auto halves = splitRects(r);
    const QRect left = halves.first;
    const QRect right = halves.second;
    // Split: each half is both the clip and the geometry, so every image is
    // centered in its own half-pane (symmetric halves). The divider is drawn at
    // the shared boundary (the right half's left edge), matching Swipe's
    // complementary-clip convention.
    drawCellCompare(p, 0, left, QRectF(left));
    drawCellCompare(p, 1, right, QRectF(right));
    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPoint(right.left(), r.top()), QPoint(right.left(), r.bottom()));
}

void CompareWorkspace::drawSwipeCompare(QPainter &p, int x)
{
    const QRect r = canvasRect();
    const int divX = qBound(0, x, r.width());
    const QRect leftClip(r.topLeft(), QSize(divX, r.height()));
    const QRect rightClip(leftClip.topRight() + QPoint(1, 0), QSize(r.width() - divX, r.height()));

    // Both images share the FULL-canvas geometry (same center + shared offset),
    // revealed by complementary clips at the divider — so the two halves always
    // align on the same shared zoom/pan.
    p.save();
    p.setClipRect(leftClip);
    drawCellCompare(p, 0, leftClip, QRectF(r));
    p.restore();
    p.save();
    p.setClipRect(rightClip);
    drawCellCompare(p, 1, rightClip, QRectF(r));
    p.restore();

    p.setPen(QPen(QColor(255, 255, 255), 2));
    p.drawLine(QPoint(divX, 0), QPoint(divX, r.height()));
    p.setBrush(QColor(255, 255, 255));
    p.drawEllipse(QPoint(divX, r.height() / 2), 4, 4);
}

// A-4.1 + H3: semi-transparent overlay blend of two images, drawn with the
// synchronized transform, and with the diff overlay kept on top (the old blend
// re-fit the images and dropped the diff overlay).
void CompareWorkspace::drawOverlayCompare(QPainter &p)
{
    if (m_cellViews.size() < 2)
        return;
    const QRect r = canvasRect();

    // Base image (full opacity), drawn with the synchronized transform.
    drawCellCompare(p, 0, r, QRectF(r));

    const QImage &img1 = m_cellViews[1]->image();
    if (img1.isNull())
        return;
    const QRectF dr = cellDestRect(1, QRectF(r));
    if (dr.isEmpty())
        return;

    p.save();
    p.setClipRect(r);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    // Blend the second image on top with user-controlled opacity (A-4.1 slider).
    p.setOpacity(std::clamp(m_overlayAlpha / 100.0, 0.0, 1.0));
    p.drawImage(dr, img1);
    p.setOpacity(1.0);
    // Keep the diff overlay visible on top of the blend (H3 fix).
    const QImage &ov = m_cellViews[1]->overlay();
    if (!ov.isNull())
    {
        p.setOpacity(m_cellViews[1]->overlayOpacity());
        p.drawImage(cellFullDestRect(1, QRectF(r)), ov);
        p.setOpacity(1.0);
    }
    p.restore();
}

// ── M23: checkerboard compare (棋盘格) ──────────────────────────────────────
// Alternating blocks show image A and image B under the SAME synchronized
// transform, so block seams reveal misalignment/color shifts instantly.

void CompareWorkspace::buildCheckerboardControls(QHBoxLayout *lay)
{
    if (!lay)
        return;
    m_checkerChk = new QCheckBox(tr("棋盘对比(&K)"), this);
    m_checkerChk->setEnabled(false);
    m_checkerChk->setToolTip(tr("棋盘格交替显示两张图像（快捷键 K），块大小可调"));
    connect(m_checkerChk, &QCheckBox::toggled, this,
            [this](bool on)
            {
                if (on)
                    exclusiveMode(m_checkerChk);
                updateCanvasModeVisibility();
                if (m_checkerSizeSlider)
                    m_checkerSizeSlider->setEnabled(on);
            });
    lay->addWidget(m_checkerChk);

    m_checkerSizeSlider = new QSlider(Qt::Horizontal, this);
    m_checkerSizeSlider->setRange(16, 256);
    m_checkerSizeSlider->setValue(m_checkerSize);
    m_checkerSizeSlider->setFixedWidth(70);
    m_checkerSizeSlider->setEnabled(false);
    m_checkerSizeSlider->setToolTip(tr("棋盘块边长（像素）"));
    connect(m_checkerSizeSlider, &QSlider::valueChanged, this,
            [this](int v)
            {
                m_checkerSize = v;
                if (m_checkerSizeLabel)
                    m_checkerSizeLabel->setText(QString("%1px").arg(v));
                if (m_checkerChk && m_checkerChk->isChecked() && m_compareCanvas)
                    m_compareCanvas->update();
            });
    lay->addWidget(m_checkerSizeSlider);
    m_checkerSizeLabel = new QLabel(QString("%1px").arg(m_checkerSize), this);
    m_checkerSizeLabel->setMinimumWidth(34);
    lay->addWidget(m_checkerSizeLabel);
}

void CompareWorkspace::drawCheckerboardCompare(QPainter &p)
{
    if (m_cellViews.size() < 2 || !m_cellViews[0] || !m_cellViews[1])
        return;
    const QRect r = canvasRect();

    // Base image fills the canvas with the synchronized transform.
    drawCellCompare(p, 0, r, QRectF(r));

    const QImage &img1 = m_cellViews[1]->image();
    if (img1.isNull())
        return;
    const QRectF dr = cellDestRect(1, QRectF(r));
    if (dr.isEmpty())
        return;

    // Build the clip region of "B" blocks: every block where (bx+by) is odd.
    const int bs = std::max(4, m_checkerSize);
    QRegion region;
    for (int by = 0, y = 0; y < r.height(); ++by, y += bs)
        for (int bx = 0, x = 0; x < r.width(); ++bx, x += bs)
            if (((bx + by) & 1) != 0)
                region += QRect(x, y, bs, bs).intersected(r);

    p.save();
    p.setClipRegion(region);
    p.setRenderHint(QPainter::SmoothPixmapTransform);
    p.drawImage(dr, img1);
    // Keep the diff overlay visible inside the B blocks.
    const QImage &ov = m_cellViews[1]->overlay();
    if (!ov.isNull())
    {
        p.setOpacity(m_cellViews[1]->overlayOpacity());
        p.drawImage(cellFullDestRect(1, QRectF(r)), ov);
        p.setOpacity(1.0);
    }
    p.restore();

    // Subtle block grid so the user can tell which region belongs to B.
    p.save();
    p.setPen(QPen(QColor(255, 255, 255, 40), 1));
    for (int x = bs; x < r.width(); x += bs)
        p.drawLine(x, 0, x, r.height());
    for (int y = bs; y < r.height(); y += bs)
        p.drawLine(0, y, r.width(), y);
    p.restore();
}

