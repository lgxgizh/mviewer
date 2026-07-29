// CompareWorkspace interaction: keyboard, mouse, event filter, pixel link (M20 P0#2).
#include "compareworkspace_p.h"

bool CompareWorkspace::eventFilter(QObject *obj, QEvent *event)
{
    const int idx = m_cellViews.indexOf(static_cast<RawImageView *>(obj));
    if (idx < 0)
        return QWidget::eventFilter(obj, event);

    if (event->type() == QEvent::Wheel)
    {
        auto *we = static_cast<QWheelEvent *>(event);
        const double factor = we->angleDelta().y() > 0 ? 1.15 : 1.0 / 1.15;
        const QPoint pos = we->position().toPoint();
        if (m_syncZoom)
        {
            m_engine.zoomAt(static_cast<double>(pos.x()), static_cast<double>(pos.y()), factor);
        }
        else
        {
            // Zoom only the hovered cell around the cursor.
            m_engine.zoomAt(static_cast<double>(pos.x()), static_cast<double>(pos.y()), factor,
                            idx);
            // Clamp to a sane range to avoid runaway zoom.
            const double s = std::clamp(m_engine.cellTransform(idx).scale, 0.05, 50.0);
            m_engine.setCellScale(idx, s);
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

    // Build a multi-line tooltip with per-marker RGB + delta across cells.
    QStringList lines;
    lines << tr("共 %1 个标记点").arg(m_linkPoints.size());
    const int n = m_engine.imageCount();
    for (int i = 0; i < m_linkPoints.size(); ++i)
    {
        const int x = qRound(m_linkPoints[i].x());
        const int y = qRound(m_linkPoints[i].y());
        QString line = tr("#%1 (%2,%3)").arg(i + 1).arg(x).arg(y);
        int baseR = -1, baseG = -1, baseB = -1;
        for (int c = 0; c < n; ++c)
        {
            const auto probe = m_engine.inspectPixel(x, y, diffBaseIndex());
            // inspectPixel returns all cells; sample from cell view image as fallback.
            int r = 0, g = 0, b = 0;
            bool ok = false;
            if (c < m_cellViews.size() && m_cellViews[c] && !m_cellViews[c]->image().isNull())
            {
                const QImage &img = m_cellViews[c]->image();
                if (x >= 0 && y >= 0 && x < img.width() && y < img.height())
                {
                    const QRgb px = img.pixel(x, y);
                    r = qRed(px);
                    g = qGreen(px);
                    b = qBlue(px);
                    ok = true;
                }
            }
            Q_UNUSED(probe);
            if (!ok)
                continue;
            if (c == 0 || c == diffBaseIndex())
            {
                baseR = r;
                baseG = g;
                baseB = b;
                line += QString("  [%1] RGB(%2,%3,%4)").arg(c).arg(r).arg(g).arg(b);
            }
            else if (baseR >= 0)
            {
                line += QString("  [%1] RGB(%2,%3,%4) Δ(%5,%6,%7)")
                            .arg(c)
                            .arg(r)
                            .arg(g)
                            .arg(b)
                            .arg(r - baseR)
                            .arg(g - baseG)
                            .arg(b - baseB);
            }
            else
            {
                line += QString("  [%1] RGB(%2,%3,%4)").arg(c).arg(r).arg(g).arg(b);
            }
        }
        lines << line;
    }
    m_linkInfoLabel->setText(tr("标记: %1").arg(m_linkPoints.size()));
    m_linkInfoLabel->setToolTip(lines.join('\n'));

    // Also push a short summary to the status bar via pixelInfo.
    if (!m_linkPoints.isEmpty())
    {
        const auto &last = m_linkPoints.last();
        emit pixelInfo(tr("像素连线 #%1 @ (%2,%3) — 悬停「标记」查看全部 RGB/Δ")
                           .arg(m_linkPoints.size())
                           .arg(qRound(last.x()))
                           .arg(qRound(last.y())));
    }
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
