// CompareWorkspace keyboard-first interaction (M20 P0#4).
#include "compareworkspace_p.h"
#include "compareworkspace_shortcuts.h"

#include <QCursor>
#include <QKeyEvent>
#include <QTimer>

#include <algorithm>

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
    beginTemporaryCompare();
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
    if (m_temporaryCompareActive)
    {
        endTemporaryCompare();
        showCompareStatus(tr("已恢复临时切换前的图像"));
        return true;
    }
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

namespace
{

QString compareShortcutName(const char *keys)
{
    const mviewer::cw::CompareShortcut *entry = mviewer::cw::findCompareShortcut(keys);
    const char *name = (entry && entry->name) ? entry->name : keys;
    return QString::fromUtf8(name ? name : "");
}

QString compareModeStatus(const char *keys)
{
    return QString::fromUtf8("模式: ") + mviewer::cw::compareShortcutText(keys);
}

QString compareToggleStatus(const char *keys, bool on, const char *whenOn, const char *whenOff)
{
    const char *suffix = on ? whenOn : whenOff;
    return compareShortcutName(keys) + QString::fromUtf8(": ") +
           QString::fromUtf8(suffix ? suffix : "");
}

int focusedUnlinkedPane(int focusIndex, const QList<RawImageView *> &views)
{
    const int count = static_cast<int>(views.size());
    if (focusIndex >= 0 && focusIndex < count)
        return focusIndex;
    for (int i = 0; i < count; ++i)
    {
        RawImageView *view = views.at(i);
        if (view && !view->selection().isEmpty())
            return i;
    }
    return count > 0 ? 0 : -1;
}

struct UnlinkedRoiTarget
{
    RawImageView *view = nullptr;
    mviewer::domain::Selection selection;
    int paneIndex = 0;
    int width = 0;
    int height = 0;
    bool ok = false;
};

UnlinkedRoiTarget unlinkedRoiTarget(int focusIndex, const QList<RawImageView *> &views)
{
    UnlinkedRoiTarget result;
    result.paneIndex = focusedUnlinkedPane(focusIndex, views);
    const int count = static_cast<int>(views.size());
    if (result.paneIndex < 0 || result.paneIndex >= count)
        return result;
    result.view = views.at(result.paneIndex);
    if (!result.view)
        return result;
    result.selection = result.view->selection();
    if (result.selection.isEmpty())
        return result;
    const QSize src = result.view->sourceSize();
    result.width = src.width();
    result.height = src.height();
    result.ok = result.width > 0 && result.height > 0;
    return result;
}

void linkedImageSize(const ImageFrame *first, int &imgW, int &imgH)
{
    imgW = 0;
    imgH = 0;
    if (!first)
        return;
    imgW = first->metadata().width > 0 ? first->metadata().width : first->width();
    imgH = first->metadata().height > 0 ? first->metadata().height : first->height();
}

void stepRoiSelection(mviewer::domain::Selection &sel, int key, bool shift, bool resize, int imgW,
                      int imgH)
{
    const int step = shift ? 10 : 1;
    if (resize)
    {
        if (key == Qt::Key_Right)
            sel.width = std::clamp(sel.width + step, 1, imgW - sel.x);
        else if (key == Qt::Key_Left)
            sel.width = (std::max)(1, sel.width - step);
        else if (key == Qt::Key_Down)
            sel.height = std::clamp(sel.height + step, 1, imgH - sel.y);
        else if (key == Qt::Key_Up)
            sel.height = (std::max)(1, sel.height - step);
        return;
    }
    if (key == Qt::Key_Left)
        sel.x = std::clamp(sel.x - step, 0, (std::max)(0, imgW - sel.width));
    else if (key == Qt::Key_Right)
        sel.x = std::clamp(sel.x + step, 0, (std::max)(0, imgW - sel.width));
    else if (key == Qt::Key_Up)
        sel.y = std::clamp(sel.y - step, 0, (std::max)(0, imgH - sel.height));
    else if (key == Qt::Key_Down)
        sel.y = std::clamp(sel.y + step, 0, (std::max)(0, imgH - sel.height));
}

} // namespace

bool CompareWorkspace::handleROIKeyboardNudge(QKeyEvent *event)
{
    const int key = event->key();
    if (key != Qt::Key_Left && key != Qt::Key_Right && key != Qt::Key_Up && key != Qt::Key_Down)
        return false;

    const auto mods = event->modifiers();
    const bool shift = (mods & Qt::ShiftModifier) != 0;
    const bool alt = (mods & Qt::AltModifier) != 0;
    const bool ctrl = (mods & Qt::ControlModifier) != 0;
    if (!alt && !shift)
        return false;

    RawImageView *focusedView = nullptr;
    int paneIndex = 0;
    int imgW = 0;
    int imgH = 0;
    mviewer::domain::Selection sel;

    if (!m_roiLinked)
    {
        const UnlinkedRoiTarget target = unlinkedRoiTarget(m_focusIndex, m_cellViews);
        if (!target.ok)
            return false;
        focusedView = target.view;
        paneIndex = target.paneIndex;
        sel = target.selection;
        imgW = target.width;
        imgH = target.height;
    }
    else if (m_lastSelection.isEmpty())
    {
        return false;
    }
    else
    {
        sel = m_lastSelection;
        linkedImageSize(m_engine.imageAt(0), imgW, imgH);
        if (imgW <= 0 || imgH <= 0)
            return false;
    }

    stepRoiSelection(sel, key, shift, ctrl && alt, imgW, imgH);
    if (focusedView)
        applySelectionFromView(focusedView, sel);
    else
        applySelectionToAll(sel);

    const QString paneInfo = m_roiLinked ? QString() : tr(" (窗格 %1)").arg(paneIndex + 1);
    showCompareStatus(tr("微调 ROI%1: X=%2 Y=%3 W=%4 H=%5")
                          .arg(paneInfo)
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
        modeName = compareModeStatus("B");
        break;
    case Qt::Key_S:
        target = m_splitChk;
        modeName = compareModeStatus("S");
        break;
    case Qt::Key_W:
        target = m_swipeChk;
        modeName = compareModeStatus("W");
        break;
    case Qt::Key_O:
    case Qt::Key_Tab:
        target = m_overlayChk;
        modeName = compareModeStatus("O");
        break;
    case Qt::Key_K:
        target = m_checkerChk;
        modeName = compareModeStatus("K");
        break;
    case Qt::Key_H:
        target = m_diffHighlightChk;
        modeName = compareModeStatus("H");
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
        showCompareStatus(QString::fromUtf8("已重置为") + compareShortcutName("Shift+0"));
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
        showCompareStatus(compareToggleStatus("Z", m_syncZoomChk->isChecked(), "开启", "关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_D && m_syncDragChk)
    {
        m_syncDragChk->setChecked(!m_syncDragChk->isChecked());
        showCompareStatus(compareToggleStatus("D", m_syncDragChk->isChecked(), "开启", "关闭"));
        event->accept();
        return true;
    }
    // Crosshair / Pixel Link / Side panel.
    if (plain && key == Qt::Key_R && m_crosshairChk)
    {
        m_crosshairChk->setChecked(!m_crosshairChk->isChecked());
        showCompareStatus(compareToggleStatus("R", m_crosshairChk->isChecked(), "开启", "关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_L && m_pixelLinkChk)
    {
        m_pixelLinkChk->setChecked(!m_pixelLinkChk->isChecked());
        showCompareStatus(compareToggleStatus("L", m_pixelLinkChk->isChecked(), "开启", "关闭"));
        event->accept();
        return true;
    }
    if (plain && key == Qt::Key_I && m_sideChk)
    {
        m_sideChk->setChecked(!m_sideChk->isChecked());
        showCompareStatus(compareToggleStatus("I", m_sideChk->isChecked(), "展开", "收起"));
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

    // Plain 0 / Ctrl+0 fit the window. Ctrl+1 is 100%. Plain 1 previews an image.
    if ((ctrl || plain) && key == Qt::Key_0)
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
        zoomActual();
        event->accept();
        return true;
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

    if (handleClipboardCompareKey(event))
        return true;

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
    // Plain 1–8 only preview images. Layout presets stay on Ctrl+2 / Ctrl+4 / Ctrl+8.
    if (plain && key >= Qt::Key_1 && key <= Qt::Key_8)
    {
        if (!event->isAutoRepeat())
            beginDigitTemporaryCompare(key - Qt::Key_0);
        event->accept();
        return true;
    }
    // ? / F1 toggles the shortcut table. The status line keeps pair guidance.
    if (plain && (key == Qt::Key_Question || key == Qt::Key_Slash || key == Qt::Key_F1))
    {
        showShortcutHelp();
        event->accept();
        return true;
    }
    return false;
}

void CompareWorkspace::keyReleaseEvent(QKeyEvent *event)
{
    if (event->isAutoRepeat())
    {
        QWidget::keyReleaseEvent(event);
        return;
    }
    if (event->key() == Qt::Key_Space && m_temporaryDigit == 0)
    {
        endTemporaryCompare();
        event->accept();
        return;
    }
    if (event->key() >= Qt::Key_1 && event->key() <= Qt::Key_8 &&
        m_temporaryDigit == event->key() - Qt::Key_0)
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
    // 3. Explicitly selected edit cell (click / prior transform)
    if (m_explicitEditIdx >= 0 && m_explicitEditIdx < count)
        return m_explicitEditIdx;
    // 4. Sticky edit index from a prior selection (never invent pane 0 here)
    if (m_editIdx >= 0 && m_editIdx < count)
        return m_editIdx;
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
    // Do not default to pane 0: with no selection/focus match, leave unset so
    // rotate/flip prompt the user instead of silently targeting cell 0.
    if (idx >= 0)
    {
        onEditCellSelected(idx);
        m_explicitEditIdx = idx;
    }
}

void CompareWorkspace::rotateCurrentCell(int degrees)
{
    const int count = m_engine.imageCount();
    if (count <= 0)
        return;

    if (m_syncRotate)
    {
        const size_t needed = static_cast<size_t>(count);
        if (m_cellAdjusts.size() < needed)
            m_cellAdjusts.resize(needed);

        std::vector<int> dirty;
        dirty.reserve(needed);
        for (int i = 0; i < count; ++i)
        {
            int rot = (m_cellAdjusts[static_cast<size_t>(i)].rotation + degrees) % 360;
            if (rot < 0)
                rot += 360;
            m_cellAdjusts[static_cast<size_t>(i)].rotation = rot;
            dirty.push_back(i);
        }

        const int targetIdx = resolveEditCell();
        int displayRot = m_cellAdjusts[0].rotation;
        if (targetIdx >= 0 && targetIdx < count)
        {
            m_editIdx = targetIdx;
            m_explicitEditIdx = targetIdx;
            displayRot = m_cellAdjusts[static_cast<size_t>(targetIdx)].rotation;
        }
        if (m_rotVal)
            m_rotVal->setText(QString::number(displayRot) + "°");

        scheduleDisplayMaterialization(dirty);
        onAdjEditFinished();
        update();
        showCompareStatus(tr("已同步旋转所有比较图（未写入文件）"));
        return;
    }

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
    const int count = m_engine.imageCount();
    if (count <= 0)
        return;

    if (m_syncRotate)
    {
        const size_t needed = static_cast<size_t>(count);
        if (m_cellAdjusts.size() < needed)
            m_cellAdjusts.resize(needed);

        std::vector<int> dirty;
        dirty.reserve(needed);
        for (int i = 0; i < count; ++i)
        {
            if (horizontal)
                m_cellAdjusts[static_cast<size_t>(i)].flipH =
                    !m_cellAdjusts[static_cast<size_t>(i)].flipH;
            else
                m_cellAdjusts[static_cast<size_t>(i)].flipV =
                    !m_cellAdjusts[static_cast<size_t>(i)].flipV;
            dirty.push_back(i);
        }

        const int targetIdx = resolveEditCell();
        if (targetIdx >= 0 && targetIdx < count)
        {
            m_editIdx = targetIdx;
            m_explicitEditIdx = targetIdx;
        }

        scheduleDisplayMaterialization(dirty);
        onAdjEditFinished();
        update();
        showCompareStatus(horizontal ? tr("已同步水平翻转所有比较图（未写入文件）")
                                     : tr("已同步垂直翻转所有比较图（未写入文件）"));
        return;
    }

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
    const bool alt = (mods == Qt::AltModifier);
    if (alt && key == Qt::Key_R)
    {
        setSyncRotate(!m_syncRotate);
        showCompareStatus(compareToggleStatus("Alt+R", m_syncRotate, "开启", "关闭"));
        event->accept();
        return true;
    }
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
