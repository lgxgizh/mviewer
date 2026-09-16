// CompareWorkspace keyboard-first interaction (M20 P0#4).
#include "compareworkspace_p.h"

#include <QKeyEvent>
#include <QTimer>

// P0-4 / M20: keyboard-first compare — day-long work without the mouse.
void CompareWorkspace::keyPressEvent(QKeyEvent *event)
{
    if (handleBasicCompareKey(event) || handleModeCompareKey(event) ||
        handleChannelCompareKey(event) || handleSyncCompareKey(event) ||
        handleAdvancedCompareKey(event))
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
    else if (event->key() == Qt::Key_PageUp || event->key() == Qt::Key_Left)
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
        showCompareStatus(m_crosshairChk->isChecked() ? tr("十字光标: 开启") : tr("十字光标: 关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_L && m_pixelLinkChk)
    {
        m_pixelLinkChk->setChecked(!m_pixelLinkChk->isChecked());
        showCompareStatus(m_pixelLinkChk->isChecked() ? tr("像素审查联动: 开启") : tr("像素审查联动: 关闭"));
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
