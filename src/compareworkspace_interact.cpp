// CompareWorkspace interaction: keyboard, mouse, event filter, pixel link (M20 P0#2).
#include "compareworkspace_p.h"

#include <cmath>

bool CompareWorkspace::eventFilter(QObject *obj, QEvent *event)
{
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
        const double anchorX = pos.x() - view->width() / 2.0;
        const double anchorY = pos.y() - view->height() / 2.0;
        // Clamp the TARGET scale to [0.05, 50.0] and derive the effective
        // factor from that clamped target, so a limit hit derives the offset
        // from the clamped factor instead of recomputing it after the offset
        // was already applied.
        const double currentScale =
            m_syncZoom ? m_engine.syncTransform().scale : m_engine.cellTransform(idx).scale;
        if (!(currentScale > 0.0) || !std::isfinite(currentScale))
            return true; // invalid baseline: consume without zooming
        const double targetScale = std::clamp(currentScale * factor, 0.05, 50.0);
        const double effectiveFactor = targetScale / currentScale;
        if (effectiveFactor == 1.0)
            return true; // at a clamp: no transform change, no repaint
        // Keep the image point under the cursor fixed: the new offset zooms the
        // old offset around the anchor by the effective factor.
        const auto zoomedOffset = [effectiveFactor](double anchor, double oldOffset)
        {
            return anchor - (anchor - oldOffset) * effectiveFactor;
        };
        // Core's SyncController is enabled only when zoom AND drag sync are
        // both on, so for the mixed modes apply the transform paintEvent
        // renders with the plain setters instead of relying on zoomAt dispatch.
        const int count = m_engine.imageCount();
        if (m_syncZoom)
        {
            m_engine.setScale(targetScale);
            for (int i = 0; i < count; ++i)
                m_engine.setCellScale(i, targetScale);
        }
        else
        {
            m_engine.setCellScale(idx, targetScale);
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
            const Vec2 h = m_engine.cellTransform(idx).offset;
            m_engine.setOffset(h.x, h.y);
        }
        else
        {
            // Fully independent: zoom only the hovered cell's own offset.
            const Vec2 o = m_engine.cellTransform(idx).offset;
            m_engine.setCellOffset(idx, zoomedOffset(anchorX, o.x), zoomedOffset(anchorY, o.y));
        }
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
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool plain = (mods == Qt::NoModifier);
    const bool ctrl = (mods == Qt::ControlModifier);

    // Space hold → temporary Blink.
    if (key == Qt::Key_Space && !event->isAutoRepeat())
    {
        if (m_blinkChk && m_blinkChk->isEnabled() && !m_blinkChk->isChecked())
        {
            m_tempBlinking = true;
            m_blinkChk->setChecked(true);
        }
        event->accept();
        return;
    }
    // ESC closes the Compare dialog.
    if (key == Qt::Key_Escape)
    {
        event->accept();
        if (auto *dlg = qobject_cast<QDialog *>(window()))
            dlg->reject();
        return;
    }
    // Continuous navigation: PageUp/Down + Left/Right arrows.
    if (plain && (key == Qt::Key_PageDown || key == Qt::Key_Right || key == Qt::Key_N))
    {
        nextPair();
        event->accept();
        return;
    }
    if (plain && (key == Qt::Key_PageUp || key == Qt::Key_Left || key == Qt::Key_P))
    {
        prevPair();
        event->accept();
        return;
    }
    // Mode toggles (explicit keyPressEvent — not Alt mnemonics).
    if (plain && key == Qt::Key_B && m_blinkChk && m_blinkChk->isEnabled())
    {
        m_blinkChk->setChecked(!m_blinkChk->isChecked());
        event->accept();
        return;
    }
    // Split / Swipe / Overlay: toggle target; exclusivity is handled by the
    // checkbox toggled handlers (only when turning ON). Calling exclusiveMode
    // before toggle would uncheck siblings even when turning OFF — harmless but
    // redundant; more importantly, setChecked(true) after exclusiveMode is fine
    // because the toggled handler also clears siblings.
    if (plain && key == Qt::Key_S && m_splitChk && m_splitChk->isEnabled())
    {
        m_splitChk->setChecked(!m_splitChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_W && m_swipeChk && m_swipeChk->isEnabled())
    {
        m_swipeChk->setChecked(!m_swipeChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_O && m_overlayChk && m_overlayChk->isEnabled())
    {
        m_overlayChk->setChecked(!m_overlayChk->isChecked());
        event->accept();
        return;
    }
    // M23: K toggles checkerboard compare (棋盘格).
    if (plain && key == Qt::Key_K && m_checkerChk && m_checkerChk->isEnabled())
    {
        m_checkerChk->setChecked(!m_checkerChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_Tab && m_overlayChk && m_overlayChk->isEnabled())
    {
        // P1: Tab toggles overlay (in addition to O) per review spec.
        m_overlayChk->setChecked(!m_overlayChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_H && m_diffHighlightChk)
    {
        m_diffHighlightChk->setChecked(!m_diffHighlightChk->isChecked());
        event->accept();
        return;
    }
    // Sync toggles.
    if (plain && key == Qt::Key_Z && m_syncZoomChk)
    {
        m_syncZoomChk->setChecked(!m_syncZoomChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_D && m_syncDragChk)
    {
        m_syncDragChk->setChecked(!m_syncDragChk->isChecked());
        event->accept();
        return;
    }
    // Crosshair / Pixel Link / Side panel.
    if (plain && key == Qt::Key_C && m_crosshairChk)
    {
        m_crosshairChk->setChecked(!m_crosshairChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_L && m_pixelLinkChk)
    {
        m_pixelLinkChk->setChecked(!m_pixelLinkChk->isChecked());
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_I && m_sideChk)
    {
        m_sideChk->setChecked(!m_sideChk->isChecked());
        event->accept();
        return;
    }
    // Fit all / Swap panes.
    if (plain && key == Qt::Key_F)
    {
        fitAll();
        event->accept();
        return;
    }
    if (plain && key == Qt::Key_X)
    {
        onSwapPanes();
        event->accept();
        return;
    }
    // Diff threshold ± ( [ / ] ).
    if (plain && (key == Qt::Key_BracketLeft || key == Qt::Key_BracketRight) && m_thresholdSlider)
    {
        const int step = (key == Qt::Key_BracketRight) ? 5 : -5;
        m_thresholdSlider->setValue(qBound(0, m_thresholdSlider->value() + step, 255));
        event->accept();
        return;
    }
    // Overlay alpha ± ( , / . ).
    if (plain && (key == Qt::Key_Comma || key == Qt::Key_Period) && m_overlayAlphaSlider)
    {
        const int step = (key == Qt::Key_Period) ? 5 : -5;
        m_overlayAlphaSlider->setValue(qBound(0, m_overlayAlphaSlider->value() + step, 100));
        event->accept();
        return;
    }
    // M20: Ctrl+2 / Ctrl+4 / Ctrl+8 → named layout presets.
    if (ctrl && (key == Qt::Key_2 || key == Qt::Key_4 || key == Qt::Key_8))
    {
        const int n = (key == Qt::Key_2) ? 2 : (key == Qt::Key_4) ? 4 : 8;
        applyLayoutPreset(n);
        event->accept();
        return;
    }
    // Plain 1–8 → N-up compare presets (M16): key N compares N images.
    if (plain && (key >= Qt::Key_1 && key <= Qt::Key_8))
    {
        const int n = key - Qt::Key_0; // '1'..'8' → 1..8
        applyLayoutPreset(n);
        event->accept();
        return;
    }
    // ? → shortcut help (title bar tip).
    if (plain && (key == Qt::Key_Question || key == Qt::Key_Slash))
    {
        showShortcutHelp();
        event->accept();
        return;
    }
    QWidget::keyPressEvent(event);
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

// P0-4: swipe divider drag. In split mode the divider is fixed; in swipe mode it follows the
// cursor.
void CompareWorkspace::mousePressEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_swipeChk && m_swipeChk->isChecked())
    {
        const int x = int(width() * m_splitPos);
        if (std::abs(event->pos().x() - x) < 12)
        {
            m_splitDragging = true;
            m_splitPos = std::clamp(event->pos().x() / double(width()), 0.05, 0.95);
            update();
            return;
        }
    }
    QWidget::mousePressEvent(event);
}

void CompareWorkspace::mouseMoveEvent(QMouseEvent *event)
{
    if (m_splitDragging && m_swipeChk && m_swipeChk->isChecked())
    {
        m_splitPos = std::clamp(event->pos().x() / double(width()), 0.05, 0.95);
        update();
        return;
    }
    if (m_swipeChk && m_swipeChk->isChecked())
    {
        const int x = int(width() * m_splitPos);
        setCursor(std::abs(event->pos().x() - x) < 12 ? Qt::SplitHCursor : Qt::ArrowCursor);
    }
    QWidget::mouseMoveEvent(event);
}

void CompareWorkspace::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_splitDragging)
    {
        m_splitDragging = false;
        update();
        return;
    }
    QWidget::mouseReleaseEvent(event);
}

void CompareWorkspace::leaveEvent(QEvent *)
{
    m_splitDragging = false;
    setCursor(Qt::ArrowCursor);
}
