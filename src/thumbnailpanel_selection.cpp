#include "thumbnailpanel_p.h"
#include "selectionmodel.h"

#include "core/SidecarStore.h"

#include <QApplication>
#include <QClipboard>
#include <QDir>
#include <QMenu>

void ThumbnailPanel::onSelectionChanged()
{
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    qint64 selBytes = 0;
    for (const QModelIndex &idx : sel)
        selBytes += m_sizeByPath.value(m_paths.value(idx.row()), 0);
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

void ThumbnailPanel::scrollToPath(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    setCurrentIndex(idx);
    scrollTo(idx, PositionAtCenter);
}

void ThumbnailPanel::selectPath(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
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
        scrollTo(idx);
        onSelectionChanged();
        return;
    }
    // Single-path focus: replace selection with this item.
    if (selectionModel())
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    else
        setCurrentIndex(idx);
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
        const int row = m_rowByPath.value(p, -1);
        if (row < 0)
            continue;
        const QModelIndex idx = m_model->index(row, 0);
        sel.select(idx, idx);
    }
    // Block currentChanged 鈫?itemClicked while we rebuild multi-select, so the
    // focus move cannot collapse SelectionModel via setCurrentImage.
    const bool wasBlocked = selectionModel()->blockSignals(true);
    selectionModel()->select(sel, QItemSelectionModel::ClearAndSelect);
    const QString focus =
        !current.isEmpty() ? current : (paths.isEmpty() ? QString() : paths.first());
    if (!focus.isEmpty())
    {
        const int row = m_rowByPath.value(focus, -1);
        if (row >= 0)
        {
            const QModelIndex idx = m_model->index(row, 0);
            selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
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

void ThumbnailPanel::batchRateSelected(int stars)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    auto &sidecar = mviewer::core::SidecarStore::instance();
    for (const QString &p : paths)
    {
        const std::string sp = p.toUtf8().toStdString();
        rs.setRating(sp, stars);
        sidecar.writeSidecar(sp);
    }
    invalidateRatings();
}

void ThumbnailPanel::batchSetColorLabelSelected(int label)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    auto &sidecar = mviewer::core::SidecarStore::instance();
    for (const QString &p : paths)
    {
        const std::string sp = p.toUtf8().toStdString();
        rs.setColorLabel(sp, label);
        sidecar.writeSidecar(sp);
    }
    invalidateRatings();
}

void ThumbnailPanel::batchSetFlagSelected(bool reject, bool pick)
{
    const QStringList paths = selectedPaths();
    if (paths.isEmpty())
        return;
    auto &rs = mviewer::core::RatingStore::instance();
    auto &sidecar = mviewer::core::SidecarStore::instance();
    for (const QString &p : paths)
    {
        const std::string sp = p.toUtf8().toStdString();
        rs.setRejected(sp, reject);
        rs.setPicked(sp, pick);
        sidecar.writeSidecar(sp);
    }
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
    connect(actClearFlag, &QAction::triggered, this, [this]() { batchSetFlagSelected(false, false); });
}
