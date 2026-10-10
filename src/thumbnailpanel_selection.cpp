#include "selectionmodel.h"
#include "thumbnailpanel_p.h"

#include "core/SidecarStore.h"

#include <QApplication>
#include <QClipboard>
#include <QCursor>
#include <QDir>
#include <QFocusEvent>
#include <QKeyEvent>
#include <QMenu>
#include <QMouseEvent>
#include <QRubberBand>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

void ThumbnailPanel::onSelectionChanged()
{
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    qint64 selBytes = 0;
    for (const QModelIndex &idx : sel)
        selBytes += m_sizeByPath.value(galleryPathKey(m_paths.value(idx.row())), 0);
    const int n = sel.size();

    // M23 P2 / Code-Review #5: keep the app-wide SelectionModel (the single
    // source of truth that Compare reads) in sync with the gallery's full
    // multi-selection. QListView::ExtendedSelection already supports Ctrl / Shift
    // / rubber-band multi-select, but only a plain single click pushed the path
    // into SelectionModel before — so Compare used to receive a single (stale)
    // image instead of the whole selection. Updating here makes the shared model
    // reflect Ctrl/Shift/box selections uniformly.
    if (m_selection)
    {
        QStringList paths;
        paths.reserve(n);
        for (const QModelIndex &idx : sel)
            paths.append(m_paths.value(idx.row()));
        const QModelIndex current = currentIndex();
        const QModelIndex fallback = sel.isEmpty() ? QModelIndex() : sel.constLast();
        const QModelIndex focused =
            current.isValid() && selectionModel()->isSelected(current) ? current : fallback;
        const QString cur = focused.isValid() ? m_paths.value(focused.row()) : QString();
        m_selection->setSelection(paths, cur);
    }

    // M23 P2 (selection UX): keep the compare affordance always discoverable.
    // It shows the live selection count, enables once 2+ images are picked,
    // and is hidden only when nothing is selected.
    if (n == 0 || m_currentDir.isEmpty())
    {
        m_compareBtn->setVisible(false);
    }
    else
    {
        m_compareBtn->setVisible(true);
        m_compareBtn->setText(QStringLiteral("比较选中 (%1)").arg(n));
        const bool canCompare = n >= 2 && n <= 8;
        m_compareBtn->setEnabled(canCompare);
        m_compareBtn->setToolTip(
            canCompare ? QStringLiteral("将选中的 %1 张图片送入对比").arg(n)
                       : QStringLiteral("需要选择 2-8 张图片才能比较（当前 %1 张）").arg(n));
    }
    emit statsChanged(m_paths.size(), m_totalBytes, n, selBytes);
}

bool ThumbnailPanel::isItemFullyVisible(const QModelIndex &idx) const
{
    if (!idx.isValid())
        return false;
    const QRect r = visualRect(idx);
    if (!r.isValid())
        return false;
    const QRect vp = viewport()->rect();
    if (m_viewMode == Filmstrip)
        return r.left() >= 0 && r.right() <= vp.width();
    if (m_viewMode == Details && horizontalScrollBar() && horizontalScrollBar()->isVisible())
        return r.top() >= 0 && r.bottom() <= vp.height() && r.left() >= 0;
    return r.top() >= 0 && r.bottom() <= vp.height();
}

void ThumbnailPanel::scrollTo(const QModelIndex &index, ScrollHint hint)
{
    if (!index.isValid())
        return;
    if (hint == EnsureVisible && isItemFullyVisible(index))
        return;
    QListView::scrollTo(index, hint);
}

void ThumbnailPanel::scrollToPath(const QString &path)
{
    const int row = m_rowByPath.value(galleryPathKey(path), -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    setCurrentIndex(idx);
    scrollTo(idx, PositionAtCenter);
}

void ThumbnailPanel::selectPath(const QString &path)
{
    const int row = m_rowByPath.value(galleryPathKey(path), -1);
    if (row < 0)
    {
        // M24 (A#8): the path may belong to an async directory rescan that has
        // not landed yet (rename/refresh/restore). Park it: buildModel applies
        // it when the model containing the path arrives. If the path never
        // appears, the next successful selectPath() overwrites it.
        if (!path.isEmpty())
            m_pendingSelect = path;
        return;
    }
    const QModelIndex idx = m_model->index(row, 0);
    if (currentIndex() == idx && selectionModel() && selectionModel()->isSelected(idx))
        return; // already the current selected item - nothing to do, no scroll jank
    // If the path is already part of a multi-selection, only move the current
    // focus - do NOT ClearAndSelect (that would collapse multi-select).
    if (selectionModel() && selectionModel()->isSelected(idx))
    {
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
        if (!isItemFullyVisible(idx))
            scrollTo(idx);
        onSelectionChanged();
        return;
    }
    // Single-path focus: replace selection with this item.
    if (selectionModel())
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    else
        setCurrentIndex(idx);
    if (!isItemFullyVisible(idx))
        scrollTo(idx); // default EnsureVisible: only scrolls when off-screen
    // A programmatic jump may update the scroll bar after this stack frame.
    // Schedule one range refresh so the newly selected distant item is promoted
    // to visible priority instead of waiting for a later repaint/scroll event.
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::selectPaths(const QStringList &paths, const QString &current)
{
    if (!selectionModel() || !m_model)
        return;
    QItemSelection sel;
    for (const QString &p : paths)
    {
        const int row = m_rowByPath.value(galleryPathKey(p), -1);
        if (row < 0)
            continue;
        const QModelIndex idx = m_model->index(row, 0);
        sel.select(idx, idx);
    }
    // Block currentChanged -> itemClicked while we rebuild multi-select, so the
    // focus move cannot collapse SelectionModel via setCurrentImage.
    const bool wasBlocked = selectionModel()->blockSignals(true);
    selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    const QString focus =
        !current.isEmpty() ? current : (paths.isEmpty() ? QString() : paths.first());
    if (!focus.isEmpty())
    {
        const int row = m_rowByPath.value(galleryPathKey(focus), -1);
        if (row >= 0)
        {
            const QModelIndex idx = m_model->index(row, 0);
            selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
            if (!isItemFullyVisible(idx))
                scrollTo(idx);
            m_selectionAnchorPath = focus;
        }
    }
    selectionModel()->blockSignals(wasBlocked);
    // Signals were blocked while the full native selection was reconstructed;
    // publish once through this panel's single selection owner.
    onSelectionChanged();
}

int ThumbnailPanel::scrollOffset() const
{
    return verticalScrollBar()->value();
}

QStringList ThumbnailPanel::selectedPaths() const
{
    QStringList r;
    for (const QModelIndex &idx : selectionModel()->selectedIndexes())
        r.append(m_paths.value(idx.row()));
    return r;
}

void ThumbnailPanel::invertSelection()
{
    if (!selectionModel() || !m_model)
        return;
    const int count = m_model->rowCount();
    QItemSelection inverted;
    for (int r = 0; r < count; ++r)
    {
        const QModelIndex idx = m_model->index(r, 0);
        if (!selectionModel()->isSelected(idx))
            inverted.select(idx, idx);
    }
    selectionModel()->select(inverted, QItemSelectionModel::ClearAndSelect);
    onSelectionChanged();
}

void ThumbnailPanel::copySelectedPaths()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    QStringList nativePaths;
    nativePaths.reserve(paths.size());
    for (const QString &p : paths)
        nativePaths.append(QDir::toNativeSeparators(p));
    QApplication::clipboard()->setText(nativePaths.join(QStringLiteral("\n")));
}

void ThumbnailPanel::copySelectedFileNames()
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    QStringList names;
    names.reserve(paths.size());
    for (const QString &p : paths)
        names.append(QFileInfo(p).fileName());
    QApplication::clipboard()->setText(names.join(QStringLiteral("\n")));
}

#include <QtConcurrent/QtConcurrent>

namespace
{
void asyncWriteSidecars(std::vector<std::string> paths)
{
    (void)QtConcurrent::run(
        [paths = std::move(paths)]()
        {
            auto &sidecar = mviewer::core::SidecarStore::instance();
            for (const auto &sp : paths)
                sidecar.writeSidecar(sp);
        });
}
} // namespace

void ThumbnailPanel::batchRateSelected(int stars)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    std::vector<std::string> toWrite;
    toWrite.reserve(paths.size());
    for (const QString &p : paths)
    {
        const std::string sp = p.toUtf8().toStdString();
        rs.setRating(sp, stars);
        toWrite.push_back(sp);
    }
    asyncWriteSidecars(std::move(toWrite));
    invalidateRatings();
}

void ThumbnailPanel::batchSetColorLabelSelected(int label)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    std::vector<std::string> toWrite;
    toWrite.reserve(paths.size());
    for (const QString &p : paths)
    {
        const std::string sp = p.toUtf8().toStdString();
        rs.setColorLabel(sp, label);
        toWrite.push_back(sp);
    }
    asyncWriteSidecars(std::move(toWrite));
    invalidateRatings();
}

void ThumbnailPanel::batchSetFlagSelected(bool reject, bool pick)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    std::vector<std::string> toWrite;
    toWrite.reserve(paths.size());
    for (const QString &p : paths)
    {
        const std::string sp = p.toUtf8().toStdString();
        rs.setRejected(sp, reject);
        rs.setPicked(sp, pick);
        toWrite.push_back(sp);
    }
    asyncWriteSidecars(std::move(toWrite));
    invalidateRatings();
}

void ThumbnailPanel::populateRatingContextMenu(QMenu *menu)
{
    if (!menu)
        return;
    menu->addSeparator();
    auto *rateMenu = menu->addMenu(tr("设置评级"));
    for (int s = 5; s >= 1; --s)
    {
        QString stars;
        for (int i = 0; i < s; ++i)
            stars += QStringLiteral("★");
        QAction *act = rateMenu->addAction(QStringLiteral("%1 (%2 星)").arg(stars).arg(s));
        connect(act, &QAction::triggered, this, [this, s]() { batchRateSelected(s); });
    }
    QAction *actClearRate = rateMenu->addAction(tr("清除评级"));
    connect(actClearRate, &QAction::triggered, this, [this]() { batchRateSelected(0); });

    auto *labelMenu = menu->addMenu(tr("设置颜色标签"));
    static const struct
    {
        const char *name;
        int id;
    } kLabels[] = {
        {"红色", 1}, {"橙色", 2}, {"黄色", 3}, {"绿色", 4}, {"蓝色", 5}, {"紫色", 6},
    };
    for (const auto &item : kLabels)
    {
        QAction *act = labelMenu->addAction(tr(item.name));
        connect(act, &QAction::triggered, this,
                [this, id = item.id]() { batchSetColorLabelSelected(id); });
    }
    QAction *actClearLabel = labelMenu->addAction(tr("无标签"));
    connect(actClearLabel, &QAction::triggered, this, [this]() { batchSetColorLabelSelected(0); });

    auto *flagMenu = menu->addMenu(tr("设置标记"));
    QAction *actPick = flagMenu->addAction(tr("标记为精选 (Pick)"));
    connect(actPick, &QAction::triggered, this, [this]() { batchSetFlagSelected(false, true); });
    QAction *actReject = flagMenu->addAction(tr("标记为排除 (Reject)"));
    connect(actReject, &QAction::triggered, this, [this]() { batchSetFlagSelected(true, false); });
    QAction *actClearFlag = flagMenu->addAction(tr("清除标记"));
    connect(actClearFlag, &QAction::triggered, this,
            [this]() { batchSetFlagSelected(false, false); });
}

namespace
{
Qt::KeyboardModifiers sanitizedPressModifiers(const QMouseEvent *event)
{
    Qt::KeyboardModifiers mods = event->modifiers();
#if defined(Q_OS_WIN)
    // Guard against a phantom Shift/Ctrl modifier from the Windows IME language
    // toggle (a Shift tap). Spontaneous events are checked against the live key.
    if (event->spontaneous())
    {
        if ((mods & Qt::ShiftModifier) && ((GetKeyState(VK_SHIFT) & 0x8000) == 0))
            mods &= ~Qt::ShiftModifier;
        if ((mods & Qt::ControlModifier) && ((GetKeyState(VK_CONTROL) & 0x8000) == 0))
            mods &= ~Qt::ControlModifier;
    }
#endif
    return mods;
}

bool modifierExtendsSelection(Qt::KeyboardModifiers mods)
{
    return (mods & (Qt::ControlModifier | Qt::ShiftModifier)) != 0;
}
} // namespace

void ThumbnailPanel::selectAnchorRange(const QModelIndex &index, Qt::KeyboardModifiers mods)
{
    int anchorRow = m_rowByPath.value(galleryPathKey(m_selectionAnchorPath), -1);
    if (anchorRow < 0 && currentIndex().isValid())
        anchorRow = currentIndex().row();
    if (anchorRow < 0)
        anchorRow = index.row();
    const int first = qMin(anchorRow, index.row());
    const int last = qMax(anchorRow, index.row());
    // Plain Shift replaces the selection with the anchor range.
    // Ctrl+Shift adds that range and keeps images outside it.
    const auto flags = (mods & Qt::ControlModifier) != 0 ? QItemSelectionModel::Select
                                                         : QItemSelectionModel::ClearAndSelect;
    const QModelIndex firstIndex = m_model->index(first, 0);
    const QModelIndex lastIndex = m_model->index(last, 0);
    selectionModel()->select(QItemSelection(firstIndex, lastIndex), flags);
    selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
}

void ThumbnailPanel::applyItemClickSelection(const QModelIndex &index, Qt::KeyboardModifiers mods)
{
    const QString path = m_paths.value(index.row());
    if ((mods & Qt::ShiftModifier) != 0)
    {
        selectAnchorRange(index, mods);
        return;
    }
    if ((mods & Qt::ControlModifier) != 0)
    {
        selectionModel()->select(index, QItemSelectionModel::Toggle);
        selectionModel()->setCurrentIndex(index, QItemSelectionModel::NoUpdate);
        m_selectionAnchorPath = path;
        return;
    }
    selectionModel()->setCurrentIndex(index, QItemSelectionModel::ClearAndSelect);
    m_selectionAnchorPath = path;
}

void ThumbnailPanel::beginEmptyAreaRubberBand(QMouseEvent *event, Qt::KeyboardModifiers mods)
{
    m_pressedOnItem = false;
    m_emptyAreaPress = true;
    m_emptyAreaModifiers = mods;
    // A marquee retargets current as the pointer crosses cells. Hold the gesture
    // flag so currentChanged does not emit itemClicked for every crossed cell.
    m_selectionGesture = true;
    if (selectionModel() && !modifierExtendsSelection(mods))
    {
        selectionModel()->clearSelection();
        m_selectionAnchorPath.clear();
    }
    if (selectionModel())
        m_rubberBaseSelection = selectionModel()->selection();
    // QListView records pressedPosition from this press. Without it, the next
    // move starts DragSelectingState from a stale origin.
    QListView::mousePressEvent(event);
}

void ThumbnailPanel::mousePressEvent(QMouseEvent *event)
{
    const bool left = event->button() == Qt::LeftButton;
    const Qt::KeyboardModifiers mods = sanitizedPressModifiers(event);
    m_selectionGesture = left && modifierExtendsSelection(mods);

    // Item clicks stay on the path anchor. IconMode on Windows can keep only
    // the clicked item for Shift ranges after a custom ClearAndSelect.
    if (!left)
    {
        m_pressedOnItem = false;
        m_emptyAreaPress = false;
        QListView::mousePressEvent(event);
        return;
    }
    const QModelIndex index = indexAt(event->pos());
    m_pressedOnItem = index.isValid();
    if (index.isValid())
    {
        m_emptyAreaPress = false;
        applyItemClickSelection(index, mods);
        event->accept();
        return;
    }
    beginEmptyAreaRubberBand(event, mods);
    event->accept();
}

void ThumbnailPanel::reapplyRubberBandBase()
{
    if (!m_emptyAreaPress || !selectionModel())
        return;
    const bool control = (m_emptyAreaModifiers & Qt::ControlModifier) != 0;
    const bool shift = (m_emptyAreaModifiers & Qt::ShiftModifier) != 0;
    if (!control && !shift)
        return;
    // Qt replaced the selection with the band. Recombine with the press-time
    // snapshot: Ctrl toggles the band (Explorer), Shift adds it.
    QItemSelection merged = m_rubberBaseSelection;
    const auto mergeFlag = control ? QItemSelectionModel::Toggle : QItemSelectionModel::Select;
    merged.merge(selectionModel()->selection(), mergeFlag);
    selectionModel()->select(merged, QItemSelectionModel::ClearAndSelect);
}

void ThumbnailPanel::mouseMoveEvent(QMouseEvent *event)
{
    // A press that began on a thumbnail must not drag-select from a stale
    // pressed index. Empty-area moves update QListView's elastic band.
    if ((event->buttons() & Qt::LeftButton) != 0 && m_pressedOnItem)
    {
        event->accept();
        return;
    }
    QListView::mouseMoveEvent(event);
    if (m_emptyAreaPress && (event->buttons() & Qt::LeftButton) != 0 &&
        state() == QAbstractItemView::DragSelectingState)
        reapplyRubberBandBase();
}

QItemSelectionModel::SelectionFlags ThumbnailPanel::selectionCommand(const QModelIndex &index,
                                                                     const QEvent *event) const
{
    // While the marquee is active, always select exactly the items under the
    // band. Ctrl/Shift are reapplied from the press snapshot afterwards so a
    // Toggle command cannot flip the same cells on every move.
    if (m_emptyAreaPress && event != nullptr && event->type() == QEvent::MouseMove &&
        state() == QAbstractItemView::DragSelectingState)
        return QItemSelectionModel::Clear | QItemSelectionModel::SelectCurrent;
    return QListView::selectionCommand(index, event);
}

void ThumbnailPanel::hideStrayRubberBand()
{
    const QList<QRubberBand *> bands = findChildren<QRubberBand *>();
    for (QRubberBand *band : bands)
    {
        if (band->isVisible())
            band->hide();
    }
    if (state() == QAbstractItemView::DragSelectingState)
        setState(QAbstractItemView::NoState);
}

void ThumbnailPanel::syncRubberBandAnchor()
{
    if (!selectionModel() || !m_model)
        return;
    const QModelIndexList selected = selectionModel()->selectedIndexes();
    if (selected.isEmpty())
    {
        m_selectionAnchorPath.clear();
        return;
    }
    QModelIndex anchor = selected.constFirst();
    for (const QModelIndex &index : selected)
    {
        if (index.row() > anchor.row())
            anchor = index;
    }
    if (!anchor.isValid() || anchor.row() < 0 || anchor.row() >= m_paths.size())
        return;
    if (selectionModel()->currentIndex() != anchor)
        selectionModel()->setCurrentIndex(anchor, QItemSelectionModel::NoUpdate);
    m_selectionAnchorPath = m_paths.at(anchor.row());
}

void ThumbnailPanel::forwardRubberBandRelease(QMouseEvent *event)
{
    if (m_endingRubberBand)
        return;
    m_endingRubberBand = true;
    const bool dragged = state() == QAbstractItemView::DragSelectingState;
    m_emptyAreaPress = false;
    QListView::mouseReleaseEvent(event);
    hideStrayRubberBand();
    if (dragged)
        syncRubberBandAnchor();
    m_rubberBaseSelection.clear();
    m_endingRubberBand = false;
}

void ThumbnailPanel::mouseReleaseEvent(QMouseEvent *event)
{
    const bool endBand = event->button() == Qt::LeftButton && m_emptyAreaPress;
    m_pressedOnItem = false;
    if (endBand)
    {
        forwardRubberBandRelease(event);
        m_selectionGesture = false;
        return;
    }
    if (event->button() == Qt::LeftButton)
    {
        m_selectionGesture = false;
        event->accept();
        return;
    }
    QListView::mouseReleaseEvent(event);
}

void ThumbnailPanel::endRubberBandIfActive()
{
    if (m_endingRubberBand || !isRubberBandActive())
        return;
    QWidget *vp = viewport();
    const QPoint local = vp != nullptr ? vp->mapFromGlobal(QCursor::pos()) : QPoint();
    const QPoint global = vp != nullptr ? vp->mapToGlobal(local) : local;
    QMouseEvent release(QEvent::MouseButtonRelease, QPointF(local), QPointF(global), Qt::LeftButton,
                        Qt::NoButton, m_emptyAreaModifiers);
    m_pressedOnItem = false;
    forwardRubberBandRelease(&release);
    m_selectionGesture = false;
}

bool ThumbnailPanel::isRubberBandActive() const
{
    return m_emptyAreaPress || state() == QAbstractItemView::DragSelectingState;
}

bool ThumbnailPanel::consumeRubberBandCancel(QEvent *event)
{
    if (event == nullptr || !isRubberBandActive())
        return false;
    const QEvent::Type type = event->type();
    if (type == QEvent::UngrabMouse || type == QEvent::WindowDeactivate)
    {
        endRubberBandIfActive();
        return false;
    }
    if (type == QEvent::ShortcutOverride)
    {
        const auto *keyEvent = static_cast<const QKeyEvent *>(event);
        if (keyEvent->key() != Qt::Key_Escape)
            return false;
        event->accept();
        return true;
    }
    if (type != QEvent::KeyPress)
        return false;
    const auto *keyEvent = static_cast<const QKeyEvent *>(event);
    if (keyEvent->key() != Qt::Key_Escape)
        return false;
    endRubberBandIfActive();
    event->accept();
    return true;
}

void ThumbnailPanel::focusOutEvent(QFocusEvent *event)
{
    endRubberBandIfActive();
    QListView::focusOutEvent(event);
}

void ThumbnailPanel::mouseDoubleClickEvent(QMouseEvent *event)
{
    // Item presses do not call QListView::mousePressEvent, so the open signal
    // cannot depend on QAbstractItemView's press tracking. Empty-area presses
    // do forward, for the rubber band only.
    if (event->button() == Qt::LeftButton)
    {
        const QModelIndex index = indexAt(event->pos());
        if (index.isValid())
        {
            event->accept();
            emit itemDoubleClicked(m_paths.value(index.row()));
            return;
        }
    }
    QListView::mouseDoubleClickEvent(event);
}
