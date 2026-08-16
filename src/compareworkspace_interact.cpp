// CompareWorkspace interaction: keyboard, mouse, event filter, pixel link (M20 P0#2).
#include "compareworkspace_p.h"

void CompareWorkspace::showShortcutHelp()
{
    // Lightweight status-bar style tip via window title flash — no modal dialog
    // so day-long keyboard work is not interrupted.
    const QString tip =
        tr("Compare 快捷键: B Blink · Space 按住Blink · S Split · W Swipe · O Overlay · "
           "K 棋盘 · H Diff高亮 · Z/D 同步缩放/拖动 · C 准星 · L 像素连线 · "
           "1~8 布局预设 · PgUp/PgDn 或 ←/→ 连续导航 · F Fit · X 交换 · ? 帮助 · Esc 关闭");
    if (auto *w = window())
        w->setWindowTitle(tip);
}

#include <cmath>

bool CompareWorkspace::eventFilter(QObject *obj, QEvent *event)
{
    // M34: the dedicated compareCanvas owns its own input while a canvas mode
    // (split / swipe / overlay / checkerboard) is active. Hidden RawImageViews
    // cannot receive wheel/drag, so route canvas events here instead.
    if (obj == m_compareCanvas)
        return canvasEventFilter(event);

    auto *view = qobject_cast<RawImageView *>(obj);
    const int idx = view ? view->cellIndex() : -1;
    if (idx < 0 || idx >= m_cellViews.size())
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel)
    {
        auto *we = static_cast<QWheelEvent *>(event);
        const int wheelDelta = we->angleDelta().y();
        if (wheelDelta == 0)
            return true; // horizontal-only wheel: consume without zooming
        const double factor = wheelDelta > 0 ? 1.15 : 1.0 / 1.15;
        // The transform anchors in CENTER-RELATIVE coordinates (RawImageView
        // stores offset as a pan delta from the pane center), so convert the
        // widget-local cursor position before applying the zoom.
        const QPoint pos = we->position().toPoint();
        applyAnchorZoom(idx, pos.x() - view->width() / 2.0, pos.y() - view->height() / 2.0, factor);
        update();
        return true;
    }

    if (event->type() == QEvent::MouseButtonPress)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
        {
            // A-4.3: in Pixel Link mode, click places a shared image-space marker.
            if (m_pixelLinkChk && m_pixelLinkChk->isChecked() && idx >= 0 &&
                idx < m_cellViews.size() && m_cellViews[idx])
            {
                const QPointF imgPt = m_cellViews[idx]->widgetToImage(me->pos());
                addLinkPoint(imgPt);
                return true; // consume — do not start pan drag
            }
            m_dragging = true;
            m_lastMouse = me->pos();
            m_dragStartPos = me->pos();
            m_dragIdx = idx;
        }
        return false;
    }

    if (event->type() == QEvent::MouseMove)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (m_dragging)
        {
            const QPoint delta = me->pos() - m_lastMouse;
            m_lastMouse = me->pos();
            if (m_syncDrag)
            {
                const Vec2 o = m_engine.syncTransform().offset;
                m_engine.setOffset(o.x + delta.x(), o.y + delta.y());
            }
            else
            {
                const Vec2 oldOff = m_engine.cellTransform(m_dragIdx).offset;
                m_engine.setCellOffset(m_dragIdx, oldOff.x + delta.x(), oldOff.y + delta.y());
            }
            update();
        }
        return false;
    }

    if (event->type() == QEvent::MouseButtonRelease)
    {
        auto *me = static_cast<QMouseEvent *>(event);
        if (me->button() == Qt::LeftButton)
        {
            m_dragging = false;
            // Click (no significant drag): select cell for editing & per-pane histogram
            const QPoint delta = me->pos() - m_dragStartPos;
            if (delta.manhattanLength() < 4)
            {
                onEditCellSelected(m_dragIdx);
                refreshHistograms();
            }
        }
        return false;
    }

    return QWidget::eventFilter(obj, event);
}

// M34: wheel + mouse input for the dedicated compareCanvas page. All canvas
// events are consumed here — the canvas owns interaction while a canvas mode
// is active, so nothing leaks to the hidden grid cells.
bool CompareWorkspace::handleCanvasWheel(QEvent *event)
{
    auto *we = static_cast<QWheelEvent *>(event);
    const int delta = we->angleDelta().y();
    if (delta == 0)
        return true;
    const double factor = delta > 0 ? 1.15 : 1.0 / 1.15;
    const QRect cr = canvasRect();
    const QPoint pos = we->position().toPoint();
    double anchorX = pos.x() - cr.width() / 2.0;
    double anchorY = pos.y() - cr.height() / 2.0;
    if (m_splitChk && m_splitChk->isChecked())
    {
        const int midX = cr.width() / 2;
        const bool left = pos.x() < midX;
        const QRect half = left ? QRect(cr.left(), cr.top(), midX, cr.height())
                                : QRect(cr.left() + midX, cr.top(), cr.width() - midX, cr.height());
        anchorX = pos.x() - (half.x() + half.width() / 2.0);
        anchorY = pos.y() - (half.y() + half.height() / 2.0);
        applyAnchorZoom(left ? 0 : 1, anchorX, anchorY, factor);
    }
    else
    {
        applyAnchorZoom(0, anchorX, anchorY, factor);
    }
    if (m_compareCanvas)
        m_compareCanvas->update();
    update();
    return true;
}

bool CompareWorkspace::handleCanvasPress(QEvent *event)
{
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::LeftButton)
    {
        const QRect cr = canvasRect();
        const bool swipe = m_swipeChk && m_swipeChk->isChecked();
        const int divider = int(cr.width() * m_splitPos);
        if (swipe && std::abs(me->pos().x() - divider) < 12)
        {
            m_splitDragging = true;
            m_splitPos = std::clamp(me->pos().x() / double(cr.width()), 0.05, 0.95);
            if (m_compareCanvas)
                m_compareCanvas->update();
            return true;
        }
        m_dragging = true;
        m_lastMouse = me->pos();
        m_dragStartPos = me->pos();
        m_dragIdx = canvasRefCellAt(me->pos());
    }
    return true;
}

bool CompareWorkspace::handleCanvasMove(QEvent *event)
{
    auto *me = static_cast<QMouseEvent *>(event);
    const QRect cr = canvasRect();
    if (m_splitDragging && m_swipeChk && m_swipeChk->isChecked())
    {
        m_splitPos = std::clamp(me->pos().x() / double(cr.width()), 0.05, 0.95);
        if (m_compareCanvas)
            m_compareCanvas->update();
        return true;
    }
    if (m_swipeChk && m_swipeChk->isChecked() && m_compareCanvas)
    {
        const int divider = int(cr.width() * m_splitPos);
        m_compareCanvas->setCursor(std::abs(me->pos().x() - divider) < 12 ? Qt::SplitHCursor
                                                                           : Qt::ArrowCursor);
    }
    if (m_dragging)
    {
        const QPoint delta = me->pos() - m_lastMouse;
        m_lastMouse = me->pos();
        if (m_syncDrag)
        {
            const Vec2 offset = m_engine.syncTransform().offset;
            m_engine.setOffset(offset.x + delta.x(), offset.y + delta.y());
        }
        else
        {
            const Vec2 oldOffset = m_engine.cellTransform(m_dragIdx).offset;
            m_engine.setCellOffset(m_dragIdx, oldOffset.x + delta.x(), oldOffset.y + delta.y());
        }
        if (m_compareCanvas)
            m_compareCanvas->update();
        update();
    }
    return true;
}

bool CompareWorkspace::handleCanvasRelease(QEvent *event)
{
    auto *me = static_cast<QMouseEvent *>(event);
    if (me->button() == Qt::LeftButton)
    {
        m_splitDragging = false;
        m_dragging = false;
        if (m_compareCanvas)
            m_compareCanvas->update();
    }
    return true;
}

bool CompareWorkspace::handleCanvasLeave(QEvent *)
{
    m_splitDragging = false;
    m_dragging = false;
    if (m_compareCanvas)
        m_compareCanvas->setCursor(Qt::ArrowCursor);
    return true;
}

bool CompareWorkspace::canvasEventFilter(QEvent *event)
{
    if (event->type() == QEvent::Paint)
    {
        paintCompareCanvas();
        return true;
    }
    if (event->type() == QEvent::Wheel)
        return handleCanvasWheel(event);
    if (event->type() == QEvent::MouseButtonPress)
        return handleCanvasPress(event);
    if (event->type() == QEvent::MouseMove)
        return handleCanvasMove(event);
    if (event->type() == QEvent::MouseButtonRelease)
        return handleCanvasRelease(event);
    if (event->type() == QEvent::Leave)
        return handleCanvasLeave(event);
    return QWidget::eventFilter(m_compareCanvas, event);
}

//
// Canvas interaction cell: in Split the hovered half maps to its image; every
// other canvas mode uses the stable reference cell 0.
int CompareWorkspace::canvasRefCellAt(const QPoint &pos) const
{
    if (m_splitChk && m_splitChk->isChecked())
    {
        const QRect cr = canvasRect();
        return pos.x() < cr.width() / 2 ? 0 : 1;
    }
    return 0;
}

// Shared anchored-zoom transform. Clamps the TARGET scale to [0.05, 50.0] and
// derives the effective factor from that clamped target, so a limit hit derives
// the offset from the clamped factor instead of recomputing it after the offset
// was already applied. Respects all four syncZoom / syncDrag combinations.
void CompareWorkspace::applyAnchorZoom(int refIdx, double anchorX, double anchorY, double factor)
{
    const double currentScale = m_engine.cellTransform(refIdx).scale;
    if (!(currentScale > 0.0) || !std::isfinite(currentScale))
        return; // invalid baseline: consume without zooming
    const double targetScale = std::clamp(currentScale * factor, 0.05, 50.0);
    const double effectiveFactor = targetScale / currentScale;
    if (effectiveFactor == 1.0)
        return; // at a clamp: no transform change, no repaint
    // Keep the image point under the cursor fixed: the new offset zooms the
    // old offset around the anchor by the effective factor.
    const auto zoomedOffset = [effectiveFactor](double anchor, double oldOffset)
    { return anchor - (anchor - oldOffset) * effectiveFactor; };
    // Core's SyncController is enabled only when zoom AND drag sync are
    // both on, so for the mixed modes apply the transform paintEvent
    // renders with the plain setters instead of relying on zoomAt dispatch.
    const int count = m_engine.imageCount();
    if (m_syncZoom)
    {
        QVector<double> oldScales;
        oldScales.reserve(count);
        for (int i = 0; i < count; ++i)
            oldScales.push_back(m_engine.cellTransform(i).scale);
        m_sharedZoomRatio *= effectiveFactor;
        m_engine.setScale(m_sharedZoomRatio);
        for (int i = 0; i < count; ++i)
            m_engine.setCellScale(i, oldScales[i] * effectiveFactor);
    }
    else
    {
        m_engine.setCellScale(refIdx, targetScale);
    }
    if (m_syncDrag)
    {
        const Vec2 o = m_engine.syncTransform().offset;
        const double ox = zoomedOffset(anchorX, o.x);
        const double oy = zoomedOffset(anchorY, o.y);
        m_engine.setOffset(ox, oy);
        for (int i = 0; i < count; ++i)
            m_engine.setCellOffset(i, ox, oy);
    }
    else if (m_syncZoom)
    {
        // Zoom sync on, drag sync off: zoom every cell's own offset
        // independently around the anchor, then refresh the shared offset
        // from the hovered cell without overwriting the cells (the
        // controller is disabled because drag sync is off).
        for (int i = 0; i < count; ++i)
        {
            const Vec2 o = m_engine.cellTransform(i).offset;
            m_engine.setCellOffset(i, zoomedOffset(anchorX, o.x), zoomedOffset(anchorY, o.y));
        }
        const Vec2 h = m_engine.cellTransform(refIdx).offset;
        m_engine.setOffset(h.x, h.y);
    }
    else
    {
        // Fully independent: zoom only the hovered cell's own offset.
        const Vec2 o = m_engine.cellTransform(refIdx).offset;
        m_engine.setCellOffset(refIdx, zoomedOffset(anchorX, o.x), zoomedOffset(anchorY, o.y));
    }
    scheduleDisplayLodRefresh(refIdx);
}

void CompareWorkspace::onPixelLinkToggled(bool on)
{
    if (m_clearLinksBtn)
        m_clearLinksBtn->setEnabled(on && !m_linkPoints.isEmpty());
    if (!on)
    {
        // Keep markers stored but hide them when mode is off.
        for (auto *v : m_cellViews)
            if (v)
                v->clearLinkMarkers();
    }
    else
    {
        refreshLinkMarkers();
    }
    updateLinkInfo();
    update();
}

void CompareWorkspace::addLinkPoint(const QPointF &imgPt)
{
    if (imgPt.x() < 0 || imgPt.y() < 0)
        return;
    // Cap at 16 markers to keep the UI readable.
    if (m_linkPoints.size() >= 16)
        m_linkPoints.removeFirst();
    m_linkPoints.push_back(imgPt);
    refreshLinkMarkers();
    updateLinkInfo();
    if (m_clearLinksBtn)
        m_clearLinksBtn->setEnabled(m_pixelLinkChk && m_pixelLinkChk->isChecked());
    update();
}

void CompareWorkspace::clearLinkPoints()
{
    m_linkPoints.clear();
    refreshLinkMarkers();
    updateLinkInfo();
    if (m_clearLinksBtn)
        m_clearLinksBtn->setEnabled(false);
    update();
}

void CompareWorkspace::refreshLinkMarkers()
{
    for (auto *v : m_cellViews)
    {
        if (!v)
            continue;
        if (m_pixelLinkChk && m_pixelLinkChk->isChecked() && !m_linkPoints.isEmpty())
            v->setLinkMarkers(m_linkPoints);
        else
            v->clearLinkMarkers();
    }
}

void CompareWorkspace::updateLinkInfo()
{
    if (!m_linkInfoLabel)
        return;
    if (m_linkPoints.isEmpty())
    {
        m_linkInfoLabel->setText(tr("标记: 0"));
        m_linkInfoLabel->setToolTip(QString());
        return;
    }

    struct LinkSample
    {
        int r = 0;
        int g = 0;
        int b = 0;
        bool valid = false;
    };
    const int n = m_engine.imageCount();
    const int baseIdx = n > 0 ? qBound(0, diffBaseIndex(), n - 1) : -1;
    QStringList lines;
    lines << tr("共 %1 个标记点").arg(m_linkPoints.size());
    for (int i = 0; i < m_linkPoints.size(); ++i)
    {
        const int x = qRound(m_linkPoints[i].x());
        const int y = qRound(m_linkPoints[i].y());
        QString line = tr("#%1 (%2,%3)").arg(i + 1).arg(x).arg(y);
        std::vector<LinkSample> samples(static_cast<size_t>(n));
        for (int c = 0; c < n; ++c)
        {
            if (c >= m_cellViews.size() || !m_cellViews[c])
                continue;
            const QImage &image = m_cellViews[c]->image();
            if (x < 0 || y < 0 || x >= image.width() || y >= image.height())
                continue;
            const QRgb pixel = image.pixel(x, y);
            samples[static_cast<size_t>(c)] = {qRed(pixel), qGreen(pixel), qBlue(pixel), true};
        }
        const bool baseValid = baseIdx >= 0 && samples[static_cast<size_t>(baseIdx)].valid;
        const LinkSample base = baseValid ? samples[static_cast<size_t>(baseIdx)] : LinkSample{};
        for (int c = 0; c < n; ++c)
        {
            const LinkSample &sample = samples[static_cast<size_t>(c)];
            if (!sample.valid)
            {
                line += QString("  [%1] 无效").arg(c);
            }
            else if (c == baseIdx)
            {
                line += QString("  [%1] RGB(%2,%3,%4)")
                            .arg(c)
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b);
            }
            else if (baseValid)
            {
                line += QString("  [%1] RGB(%2,%3,%4) Δ(%5,%6,%7)")
                            .arg(c)
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b)
                            .arg(sample.r - base.r)
                            .arg(sample.g - base.g)
                            .arg(sample.b - base.b);
            }
            else
            {
                line += QString("  [%1] RGB(%2,%3,%4) Δ无效")
                            .arg(c)
                            .arg(sample.r)
                            .arg(sample.g)
                            .arg(sample.b);
            }
        }
        lines << line;
    }
    m_linkInfoLabel->setText(tr("标记: %1").arg(m_linkPoints.size()));
    m_linkInfoLabel->setToolTip(lines.join('\n'));
    const auto &last = m_linkPoints.last();
    emit pixelInfo(tr("像素连线 #%1 @ (%2,%3) — 悬停“标记”查看全部 RGB/Δ")
                       .arg(m_linkPoints.size())
                       .arg(qRound(last.x()))
                       .arg(qRound(last.y())));
}

void CompareWorkspace::drawPixelLinkLines(QPainter &p)
{
    // Draw dashed connectors between the same marker index on cell 0 and cell 1
    // when both cells are visible in the grid (not split/swipe/overlay modes).
    if (m_cellViews.size() < 2 || !m_cellViews[0] || !m_cellViews[1])
        return;
    if (isSplitOrSwipe() || (m_overlayChk && m_overlayChk->isChecked()))
        return;

    auto mapToWorkspace = [this](RawImageView *view, const QPointF &imgPt) -> QPointF
    {
        if (!view || view->image().isNull() || view->scale() <= 0.0)
            return {};
        // Image → widget (view-local), then map to this workspace.
        const double sc = view->scale();
        const QPointF off = view->offset();
        const double cx = view->width() / 2.0 + off.x();
        const double cy = view->height() / 2.0 + off.y();
        const double dw = view->image().width() * sc;
        const double dh = view->image().height() * sc;
        const double wx = cx - dw / 2.0 + imgPt.x() * sc;
        const double wy = cy - dh / 2.0 + imgPt.y() * sc;
        return view->mapTo(this, QPoint(qRound(wx), qRound(wy)));
    };

    QPen pen(QColor(0xFF, 0x66, 0x66, 180), 1, Qt::DashLine);
    pen.setCosmetic(true);
    p.setPen(pen);
    for (const QPointF &pt : m_linkPoints)
    {
        const QPointF a = mapToWorkspace(m_cellViews[0], pt);
        const QPointF b = mapToWorkspace(m_cellViews[1], pt);
        if (a.isNull() || b.isNull())
            continue;
        p.drawLine(a, b);
    }
}

// P0-4 / M20: keyboard-first compare — day-long work without the mouse.
void CompareWorkspace::keyPressEvent(QKeyEvent *event)
{
    if (handleBasicCompareKey(event) || handleModeCompareKey(event) ||
        handleSyncCompareKey(event) || handleAdvancedCompareKey(event))
        return;
    QWidget::keyPressEvent(event);
}

bool CompareWorkspace::handleBasicCompareKey(QKeyEvent *event)
{
    return handleBasicCompareSpace(event) || handleBasicCompareEscape(event) ||
           handleBasicCompareNavigation(event);
}

bool CompareWorkspace::handleBasicCompareSpace(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Space || event->isAutoRepeat())
        return false;
    if (m_blinkChk && m_blinkChk->isEnabled() && !m_blinkChk->isChecked())
    {
        m_tempBlinking = true;
        m_blinkChk->setChecked(true);
    }
    event->accept();
    return true;
}

bool CompareWorkspace::handleBasicCompareEscape(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Escape)
        return false;
    event->accept();
    if (auto *dlg = qobject_cast<QDialog *>(window()))
        dlg->reject();
    return true;
}

bool CompareWorkspace::handleBasicCompareNavigation(QKeyEvent *event)
{
    if (event->modifiers() != Qt::NoModifier)
        return false;
    if (event->key() == Qt::Key_PageDown || event->key() == Qt::Key_Right ||
        event->key() == Qt::Key_N)
        nextPair();
    else if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_Left ||
             event->key() == Qt::Key_P)
        prevPair();
    else
        return false;
    event->accept();
    return true;
}

bool CompareWorkspace::handleModeCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    if (event->modifiers() != Qt::NoModifier)
        return false;
    QCheckBox *target = nullptr;
    switch (key)
    {
    case Qt::Key_B:
        target = m_blinkChk;
        break;
    case Qt::Key_S:
        target = m_splitChk;
        break;
    case Qt::Key_W:
        target = m_swipeChk;
        break;
    case Qt::Key_O:
    case Qt::Key_Tab:
        target = m_overlayChk;
        break;
    case Qt::Key_K:
        target = m_checkerChk;
        break;
    case Qt::Key_H:
        target = m_diffHighlightChk;
        break;
    default:
        return false;
    }
    if (!target || (target != m_diffHighlightChk && !target->isEnabled()))
        return false;
    target->setChecked(!target->isChecked());
    event->accept();
    return true;
}

bool CompareWorkspace::handleSyncCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool plain = (mods == Qt::NoModifier);
    const bool ctrl = (mods == Qt::ControlModifier);
    // Sync toggles.
    if (plain && key == Qt::Key_Z && m_syncZoomChk)
    {
        m_syncZoomChk->setChecked(!m_syncZoomChk->isChecked());
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_D && m_syncDragChk)
    {
        m_syncDragChk->setChecked(!m_syncDragChk->isChecked());
        event->accept();
        return true;
    }
    // Crosshair / Pixel Link / Side panel.
    if (plain && key == Qt::Key_C && m_crosshairChk)
    {
        m_crosshairChk->setChecked(!m_crosshairChk->isChecked());
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_L && m_pixelLinkChk)
    {
        m_pixelLinkChk->setChecked(!m_pixelLinkChk->isChecked());
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_I && m_sideChk)
    {
        m_sideChk->setChecked(!m_sideChk->isChecked());
        event->accept();
        return true;
    }
    // Fit all / Swap panes.
    if (plain && key == Qt::Key_F)
    {
        fitAll();
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_X)
    {
        onSwapPanes();
        event->accept();
        return true;
    }
    return false;
}

bool CompareWorkspace::handleAdvancedCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool plain = (mods == Qt::NoModifier);
    const bool ctrl = (mods == Qt::ControlModifier);
    // Diff threshold ± ( [ / ] ).
    if (plain && (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight) && m_thresholdSlider)
    {
        const int step = (key == Qt::Key_BracketRight) ? 5 : -5;
        m_thresholdSlider->setValue(qBound(0, m_thresholdSlider->value() + step, 255));
        event->accept();
        return true;
    }
    // Overlay alpha ± ( , / . ).
    if (plain && (key == Qt::Key_Comma || key == Qt::Key_Period) && m_overlayAlphaSlider)
    {
        const int step = (key == Qt::Key_Period) ? 5 : -5;
        m_overlayAlphaSlider->setValue(qBound(0, m_overlayAlphaSlider->value() + step, 100));
        event->accept();
        return true;
    }
    // M20: Ctrl+2 / Ctrl+4 / Ctrl+8 → named layout presets.
    if (ctrl && (key == Qt::Key_2 || key == Qt::Key_4 || key == Qt::Key_8))
    {
        const int n = (key == Qt::Key_2) ? 2 : (key == Qt::Key_4) ? 4 : 8;
        applyLayoutPreset(n);
        event->accept();
        return true;
    }
    // Plain 1–8 → N-up compare presets (M16): key N compares N images.
    if (plain && (key >= Qt::Key_1 && key <= Qt::Key_8))
    {
        const int n = key - Qt::Key_0; // '1'..'8' → 1..8
        applyLayoutPreset(n);
        event->accept();
        return true;
    }
    // ? → shortcut help (title bar tip).
    if (plain && (key == Qt::Key_Question || key == Qt::Key_Slash))
    {
        showShortcutHelp();
        event->accept();
        return true;
    }
    return false;
}

void CompareWorkspace::keyReleaseEvent(QKeyEvent *event)
{
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat() && m_tempBlinking)
    {
        m_tempBlinking = false;
        if (m_blinkChk && m_blinkChk->isChecked())
            m_blinkChk->setChecked(false);
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}
