// CompareWorkspace keyboard-first interaction (M20 P0#4).
#include "compareworkspace_p.h"

#include <QKeyEvent>
#include <QTimer>

// P0-4 / M20: keyboard-first compare — day-long work without the mouse.
void CompareWorkspace::keyPressEvent(QKeyEvent *event)
{
    if (handleBasicCompareKey(event) || handleModeCompareKey(event) ||
        handleChannelCompareKey(event) || handleSyncCompareKey(event) ||
        handleZoomCompareKey(event) || handleAdvancedCompareKey(event))
        return;
    QWidget::keyPressEvent(event);
}

bool CompareWorkspace::handleBasicCompareKey(QKeyEvent *event)
{
    return handleBasicCompareSpace(event) || handleBasicCompareEscape(event) ||
           handleBasicCompareNavigation(event) || handleROIKeyboardNudge(event);
}

bool CompareWorkspace::handleBasicCompareSpace(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Space || event->isAutoRepeat())
        return false;
    if (m_temporaryCompareButton && m_temporaryCompareButton->isEnabled())
    {
        beginTemporaryCompare();
    }
    event->accept();
    return true;
}

void CompareWorkspace::closeCompareHost()
{
    if (auto *dlg = qobject_cast<QDialog *>(window()))
        dlg->reject();
}

void CompareWorkspace::showCompareStatus(const QString &text, int msec)
{
    if (!m_compareStatusLabel)
        return;
    m_compareStatusLabel->setText(text);
    QTimer::singleShot(msec, m_compareStatusLabel,
                       [label = QPointer<QLabel>(m_compareStatusLabel), text]()
                       {
                           if (label && label->text() == text)
                               label->clear();
                       });
}

bool CompareWorkspace::handleBasicCompareEscape(QKeyEvent *event)
{
    if (event->key() != Qt::Key_Escape)
        return false;
    event->accept();
    if (!m_lastSelection.isEmpty())
    {
        clearROI();
        showCompareStatus(tr("选区已清除，再按 Esc 退出"));
        return true;
    }
    closeCompareHost();
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

bool CompareWorkspace::handleROIKeyboardNudge(QKeyEvent *event)
{
    if (m_lastSelection.isEmpty() || !m_roiLinked)
        return false;

    const int key = event->key();
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down)
        return false;

    const auto mods = event->modifiers();
    const bool shift = (mods & Qt::ShiftModifier);
    const bool alt = (mods & Qt::AltModifier);
    const bool ctrl = (mods & Qt::ControlModifier);

    if (!alt && !shift)
        return false;

    const ImageFrame *first = m_engine.imageAt(0);
    const int imgW =
        first ? (first->metadata().width > 0 ? first->metadata().width : first->width()) : 0;
    const int imgH =
        first ? (first->metadata().height > 0 ? first->metadata().height : first->height()) : 0;
    if (imgW <= 0 || imgH <= 0)
        return false;

    const int step = shift ? 10 : 1;
    mviewer::domain::Selection sel = m_lastSelection;

    if (ctrl && alt)
    {
        if (key == Qt::Key_Right)
            sel.width = std::clamp(sel.width + step, 1, imgW - sel.x);
        else if (key == Qt::Key_Left)
            sel.width = std::max(1, sel.width - step);
        else if (key == Qt::Key_Down)
            sel.height = std::clamp(sel.height + step, 1, imgH - sel.y);
        else if (key == Qt::Key_Up)
            sel.height = std::max(1, sel.height - step);
    }
    else
    {
        if (key == Qt::Key_Left)
            sel.x = std::clamp(sel.x - step, 0, std::max(0, imgW - sel.width));
        else if (key == Qt::Key_Right)
            sel.x = std::clamp(sel.x + step, 0, std::max(0, imgW - sel.width));
        else if (key == Qt::Key_Up)
            sel.y = std::clamp(sel.y - step, 0, std::max(0, imgH - sel.height));
        else if (key == Qt::Key_Down)
            sel.y = std::clamp(sel.y + step, 0, std::max(0, imgH - sel.height));
    }

    applySelectionToAll(sel);
    showCompareStatus(tr("微调 ROI: X=%1 Y=%2 W=%3 H=%4")
                          .arg(m_lastSelection.x)
                          .arg(m_lastSelection.y)
                          .arg(m_lastSelection.width)
                          .arg(m_lastSelection.height));
    event->accept();
    return true;
}

bool CompareWorkspace::handleModeCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    if (event->modifiers() != Qt::NoModifier)
        return false;
    QCheckBox *target = nullptr;
    QString modeName;
    switch (key)
    {
    case Qt::Key_B:
        target = m_blinkChk;
        modeName = tr("模式: 闪烁对比 (B)");
        break;
    case Qt::Key_S:
        target = m_splitChk;
        modeName = tr("模式: 左右分割 (S)");
        break;
    case Qt::Key_W:
        target = m_swipeChk;
        modeName = tr("模式: 卷帘对比 (W)");
        break;
    case Qt::Key_O:
    case Qt::Key_Tab:
        target = m_overlayChk;
        modeName = tr("模式: 叠加对比 (O)");
        break;
    case Qt::Key_K:
        target = m_checkerChk;
        modeName = tr("模式: 棋盘对比 (K)");
        break;
    case Qt::Key_H:
        target = m_diffHighlightChk;
        modeName = tr("模式: 差异高亮 (H)");
        break;
    default:
        return false;
    }
    if (!target || (target != m_diffHighlightChk && !target->isEnabled()))
        return false;
    const bool newState = !target->isChecked();
    target->setChecked(newState);
    showCompareStatus(newState ? modeName : tr("模式已关闭"));
    event->accept();
    return true;
}

bool CompareWorkspace::handleChannelCompareKey(QKeyEvent *event)
{
    if (event->modifiers() != Qt::ShiftModifier)
        return false;
    const int key = event->key();
    if (key == Qt::Key_0)
    {
        setOverlayMode(mviewer::OverlayMode::None);
        showCompareStatus(tr("已重置为全色彩通道"));
        event->accept();
        return true;
    }
    if (key < Qt::Key_1 || key > Qt::Key_6)
        return false;
    static const mviewer::OverlayMode kModes[] = {
        mviewer::OverlayMode::None,     mviewer::OverlayMode::ChannelR,
        mviewer::OverlayMode::ChannelG, mviewer::OverlayMode::ChannelB,
        mviewer::OverlayMode::ChannelY, mviewer::OverlayMode::ChannelV,
    };
    setOverlayMode(kModes[key - Qt::Key_1]);
    event->accept();
    return true;
}

bool CompareWorkspace::handleSyncCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool plain = (mods == Qt::NoModifier);
    if (handleTransformCompareKey(event))
        return true;
    // Sync toggles.
    if (plain && key == Qt::Key_Z && m_syncZoomChk)
    {
        m_syncZoomChk->setChecked(!m_syncZoomChk->isChecked());
        showCompareStatus(m_syncZoomChk->isChecked() ? tr("同步缩放: 开启") : tr("同步缩放: 关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_D && m_syncDragChk)
    {
        m_syncDragChk->setChecked(!m_syncDragChk->isChecked());
        showCompareStatus(m_syncDragChk->isChecked() ? tr("同步平移: 开启") : tr("同步平移: 关闭"));
        event->accept();
        return true;
    }
    // Crosshair / Pixel Link / Side panel.
    if (plain && key == Qt::Key_R && m_crosshairChk)
    {
        m_crosshairChk->setChecked(!m_crosshairChk->isChecked());
        showCompareStatus(m_crosshairChk->isChecked() ? tr("十字光标: 开启")
                                                      : tr("十字光标: 关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_L && m_pixelLinkChk)
    {
        m_pixelLinkChk->setChecked(!m_pixelLinkChk->isChecked());
        showCompareStatus(m_pixelLinkChk->isChecked() ? tr("像素审查联动: 开启")
                                                      : tr("像素审查联动: 关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_I && m_sideChk)
    {
        m_sideChk->setChecked(!m_sideChk->isChecked());
        showCompareStatus(m_sideChk->isChecked() ? tr("分析面板: 展开") : tr("分析面板: 收起"));
        event->accept();
        return true;
    }
    // Fit all / Swap panes.
    if (plain && key == Qt::Key_F)
    {
        fitAll();
        showCompareStatus(tr("视图自适应窗口 (Fit)"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_X)
    {
        onSwapPanes();
        showCompareStatus(tr("已对调 A/B 窗格"));
        event->accept();
        return true;
    }
    return false;
}

bool CompareWorkspace::handleZoomCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool plain = (mods == Qt::NoModifier);
    const bool ctrl = (mods == Qt::ControlModifier);

    // Ctrl+0 -> Fit, Ctrl+1 -> 100% actual size.
    if (ctrl && key == Qt::Key_0)
    {
        fitAll();
        showCompareStatus(tr("视图自适应窗口 (Fit)"));
        if (m_compareCanvas)
            m_compareCanvas->update();
        update();
        event->accept();
        return true;
    }
    if (ctrl && key == Qt::Key_1)
    {
        const double currentScale = m_engine.cellTransform(0).scale;
        if (currentScale > 0.0)
        {
            applyAnchorZoom(0, 0.0, 0.0, 1.0 / currentScale);
            showCompareStatus(tr("视图缩放: 100% (原始大小)"));
            if (m_compareCanvas)
                m_compareCanvas->update();
            update();
            event->accept();
            return true;
        }
    }
    // Zoom in / out (+ / = / - / _).
    const bool isZoomIn = (key == Qt::Key_Plus || key == Qt::Key_Equal);
    const bool isZoomOut = (key == Qt::Key_Minus || key == Qt::Key_Underscore);
    if ((plain || ctrl || mods == Qt::ShiftModifier) && (isZoomIn || isZoomOut))
    {
        const double factor = isZoomIn ? 1.15 : (1.0 / 1.15);
        applyAnchorZoom(0, 0.0, 0.0, factor);
        const double currentScale = m_engine.cellTransform(0).scale;
        const int pct = static_cast<int>(std::round(currentScale * 100.0));
        showCompareStatus(isZoomIn ? tr("视图放大 (%1%)").arg(pct) : tr("视图缩小 (%1%)").arg(pct));
        if (m_compareCanvas)
            m_compareCanvas->update();
        update();
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
    if (event->key() == Qt::Key_Space && !event->isAutoRepeat())
    {
        endTemporaryCompare();
        event->accept();
        return;
    }
    QWidget::keyReleaseEvent(event);
}

namespace
{
int findCellUnderMouse(const QVector<RawImageView *> &cellViews)
{
    const QPoint globalPos = QCursor::pos();
    for (int i = 0; i < cellViews.size(); ++i)
    {
        RawImageView *v = cellViews.at(i);
        if (v && v->isVisible())
        {
            const QPoint localPos = v->mapFromGlobal(globalPos);
            if (v->rect().contains(localPos))
                return i;
        }
    }
    return -1;
}
} // namespace

int CompareWorkspace::resolveEditCell() const
{
    const int count = static_cast<int>(m_cellViews.size());
    if (count <= 0)
        return -1;
    // 1. Pane under the mouse has top priority
    const int under = findCellUnderMouse(m_cellViews);
    if (under >= 0 && under < count)
        return under;
    if (m_hoverIdx >= 0 && m_hoverIdx < count)
        return m_hoverIdx;
    // 2. Explicitly focused compare cell
    if (m_focusIndex >= 0 && m_focusIndex < count)
        return m_focusIndex;
    // 3. Explicitly selected edit cell
    if (m_explicitEditIdx >= 0 && m_explicitEditIdx < count)
        return m_explicitEditIdx;
    return -1;
}

void CompareWorkspace::syncEditCellAfterLoad()
{
    const int count = static_cast<int>(m_cellViews.size());
    m_editIdx = -1;
    m_explicitEditIdx = -1;
    if (count <= 0)
        return;
    int idx = -1;
    if (m_selection)
        idx = comparedImages().indexOf(m_selection->currentImage());
    if (idx < 0 && m_focusIndex >= 0 && m_focusIndex < count)
        idx = m_focusIndex;
    onEditCellSelected(idx >= 0 ? idx : 0);
    m_explicitEditIdx = -1;
}

void CompareWorkspace::rotateCurrentCell(int degrees)
{
    const int idx = resolveEditCell();
    if (idx < 0)
    {
        showCompareStatus(tr("请先点击要旋转的窗格（仅预览，不修改原文件）"));
        return;
    }
    const int needed = idx + 1;
    if (static_cast<int>(m_cellAdjusts.size()) < needed)
        m_cellAdjusts.resize(static_cast<size_t>(needed));

    int rot = (m_cellAdjusts[static_cast<size_t>(idx)].rotation + degrees) % 360;
    if (rot < 0)
        rot += 360;
    m_cellAdjusts[static_cast<size_t>(idx)].rotation = rot;
    m_editIdx = idx;
    m_explicitEditIdx = idx;
    if (m_rotVal)
        m_rotVal->setText(QString::number(rot) + "°");
    applyAdjToCell(idx);
    onAdjEditFinished();
    update();
    const ImageFrame *img = m_engine.imageAt(idx);
    const QString name =
        img ? QString::fromStdString(img->metadata().fileName) : tr("窗格 %1").arg(idx + 1);
    showCompareStatus(tr("已预览旋转 %1（未写入文件）").arg(name));
}

void CompareWorkspace::flipCurrentCell(bool horizontal)
{
    const int idx = resolveEditCell();
    if (idx < 0)
    {
        showCompareStatus(tr("请先点击要翻转的窗格（仅预览，不修改原文件）"));
        return;
    }
    const int needed = idx + 1;
    if (static_cast<int>(m_cellAdjusts.size()) < needed)
        m_cellAdjusts.resize(static_cast<size_t>(needed));

    if (horizontal)
        m_cellAdjusts[static_cast<size_t>(idx)].flipH =
            !m_cellAdjusts[static_cast<size_t>(idx)].flipH;
    else
        m_cellAdjusts[static_cast<size_t>(idx)].flipV =
            !m_cellAdjusts[static_cast<size_t>(idx)].flipV;
    m_editIdx = idx;
    m_explicitEditIdx = idx;
    applyAdjToCell(idx);
    onAdjEditFinished();
    update();
    const ImageFrame *img = m_engine.imageAt(idx);
    const QString name =
        img ? QString::fromStdString(img->metadata().fileName) : tr("窗格 %1").arg(idx + 1);
    showCompareStatus(horizontal ? tr("已预览水平翻转 %1（未写入文件）").arg(name)
                                 : tr("已预览垂直翻转 %1（未写入文件）").arg(name));
}

bool CompareWorkspace::handleTransformCompareKey(QKeyEvent *event)
{
    const int key = event->key();
    const auto mods = event->modifiers();
    const bool ctrl = (mods == Qt::ControlModifier);
    const bool shiftCtrl = (mods == (Qt::ControlModifier | Qt::ShiftModifier));
    if (ctrl && key == Qt::Key_R)
    {
        rotateCurrentCell(90);
        event->accept();
        return true;
    }
    if (shiftCtrl && key == Qt::Key_R)
    {
        rotateCurrentCell(-90);
        event->accept();
        return true;
    }
    if (shiftCtrl && key == Qt::Key_H)
    {
        flipCurrentCell(true);
        event->accept();
        return true;
    }
    if (shiftCtrl && key == Qt::Key_V)
    {
        flipCurrentCell(false);
        event->accept();
        return true;
    }
    return false;
}
