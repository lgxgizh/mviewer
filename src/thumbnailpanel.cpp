#include "thumbnailpanel_p.h"
#include "thumbnailprovider.h"

// P0#3: DetailsHeader (the column-title strip above the Details list) is now
// defined in thumbnailpanel_p.h alongside the shared DetailLayout geometry, so
// it stays in sync with the delegate cells and keeps this TU lean.

// ---- ThumbnailPanel ---------------------------------------------------------

ThumbnailPanel::ThumbnailPanel(QWidget *parent) : QListView(parent)
{
    // QListView:: prefix disambiguates from our own ThumbnailPanel::setViewMode().
    QListView::setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    setWrapping(true);
    setUniformItemSizes(true); // all cells identical -> cheap layout for huge lists
    setSpacing(6);
    setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setTextElideMode(Qt::ElideRight);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAttribute(Qt::WA_Hover);
    viewport()->setMouseTracking(true);

    m_model = new QStringListModel(this);
    setModel(m_model);

    m_delegate = new ThumbDelegate(this, this);
    setItemDelegate(m_delegate);

    m_compareBtn = new QPushButton("比较选中", this);
    m_compareBtn->setVisible(false);
    connect(m_compareBtn, &QPushButton::clicked, this, &ThumbnailPanel::onCompareClicked);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &ThumbnailPanel::onSelectionChanged);

    // Drive thumbnail decode priority from the viewport (P0 #②).
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailPanel::updateVisibleRange);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailPanel::updateVisibleRange);

    // Restore path-based navigation signals (used by MainWindow to open images
    // and refresh the metadata panel) from the view's built-in index signals.
    connect(this, &QAbstractItemView::clicked, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemClicked(m_paths.value(idx.row()));
            });
    connect(this, &QAbstractItemView::doubleClicked, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemDoubleClicked(m_paths.value(idx.row()));
            });
    // Keyboard parity: Enter opens the viewer (same as double-click), and
    // moving the current item with the arrow keys drives the shared selection
    // model so the preview/status bar follow without a mouse. The central
    // SelectionModel no-ops on a same-path set, so selectPath() → currentChanged
    // → itemClicked cannot loop.
    connect(this, &QAbstractItemView::activated, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemDoubleClicked(m_paths.value(idx.row()));
            });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &)
            {
                if (current.isValid())
                    emit itemClicked(m_paths.value(current.row()));
            });

    // Wire the shared pipeline ONCE. The decode step is disk-cache-aware (so a
    // previously visited folder loads instantly without re-decoding), and the
    // result callback lands the QPixmap into our ready map + repaints the cell.
    // The pipeline is a singleton used only by the gallery, so configuring it
    // here is safe.
    m_alive = std::make_shared<std::atomic<bool>>(true);
    if (!m_pipelineWired)
    {
        m_pipelineWired = true;
        ThumbnailPipeline::instance().thumbSize = m_thumbSize;
        // Decode/scale/cache policy now lives in ThumbnailProvider; this TU only
        // routes the finished pixmap into its own ready map and manages lifecycle.
        ThumbnailPipeline::instance().setDecodeFn([](const std::string &p, int size)
                                                  { return ThumbnailProvider::decode(p, size); });
        auto alive = m_alive;
        ThumbnailPipeline::instance().setResultFn(
            [this, alive](const std::string &p, const ImageData &img)
            {
                if (!alive->load())
                    return; // panel destroyed; ignore late callback
                QString qp = QString::fromStdString(p);
                QPixmap pm = ThumbnailProvider::produce(p, img, m_thumbSize);
                if (pm.isNull())
                {
                    // Record the failure so the delegate can paint a distinct
                    // "无法加载" placeholder instead of the generic loading grey.
                    {
                        QMutexLocker l(&m_thumbMtx);
                        m_thumbFailed.insert(qp);
                        m_thumbPending.remove(qp);
                    }
                    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
                    return;
                }
                {
                    QMutexLocker lk(&m_thumbMtx);
                    m_thumbReady[qp] = pm;
                    m_thumbPending.remove(qp);
                }
                QMetaObject::invokeMethod(this, "onThumbReady", Qt::QueuedConnection,
                                          Q_ARG(QString, qp));
            });
    }
}

ThumbnailPanel::~ThumbnailPanel()
{
    if (m_alive)
        *m_alive = false;
    // Detach from the shared pipeline so its worker thread can't call back into
    // a destroyed panel (and restore the default decode for the next panel).
    ThumbnailPipeline::instance().setResultFn([](const std::string &, const ImageData &) {});
    ThumbnailPipeline::instance().setDecodeFn([](const std::string &p, int size)
                                              { return Decoder::decodeScaled(p, size); });
}

namespace
{
// M23 P2: the type-filter rule, factored so the on-thread directory scan and the
// off-thread enumeration share exactly one implementation.
bool passesTypeFilter(const QString &typeFilter, const QString &suffixRaw)
{
    if (typeFilter.isEmpty())
        return true;
    const QString suffix = suffixRaw.toLower();
    static const QStringList rawExts = {"cr2", "cr3", "nef", "arw", "dng", "raf", "rw2",
                                        "orf", "sr2", "srw", "pef", "3fr", "mef", "erf",
                                        "mrw", "dcr", "kdc", "mos", "raw", "iiq"};
    for (const QString &ext : typeFilter.split(','))
    {
        const QString lowered = ext.trimmed().toLower();
        if (lowered == suffix)
            return true;
        // P0: Expand "raw" alias to common RAW file extensions.
        if (lowered == "raw" && rawExts.contains(suffix))
            return true;
        // P0: Expand "tiff" alias to "tif" + "tiff".
        if (lowered == "tiff" && (suffix == "tif" || suffix == "tiff"))
            return true;
    }
    return false;
}
} // namespace

void ThumbnailPanel::setDirectory(const QString &path)
{
    m_currentDir = path;
    m_filterText.clear();
    m_filterRecursive = false;

    // P0-1 (perf): a new directory generation. Any in-flight background
    // dimension resolve or directory scan from the previous folder is
    // invalidated by the bump.
    ++m_dirGen;
    const int gen = m_dirGen;
    m_dimsResolved = false;

    // M23 P2 (first-screen): paint the (empty) directory shell immediately so a
    // 1000-image folder shows its grid in well under 1s, then scan the disk off
    // the UI thread and stream the real entries in once they are ready.
    m_allEntries.clear();
    m_paths.clear();
    m_rowByPath.clear();
    m_sizeByPath.clear();
    m_model->setStringList({});
    viewport()->update();
    emit statsChanged(0, 0, 0, 0);
    QApplication::setOverrideCursor(Qt::BusyCursor);

    // Snapshot the criteria the worker needs so it never reads volatile members.
    const QString typeFilter = m_typeFilter;
    const SortMode sortMode = m_sortMode;
    const bool sortAscending = m_sortAscending;
    auto alive = m_alive;

    std::thread(
        [this, gen, alive, path, typeFilter, sortMode, sortAscending]()
        {
            QList<Entry> entries;
            QDir dir(path);
            if (dir.exists())
            {
                const QFileInfoList list = sortedEntries(dir, sortMode, sortAscending);
                for (const QFileInfo &fi : list)
                {
                    if (!passesTypeFilter(typeFilter, fi.suffix()))
                        continue;
                    // P0-1 (perf): no pixel dimensions here — resolved lazily in
                    // the background for the Details view (see ensureDimensions).
                    entries.append(
                        {fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0, fi.lastModified()});
                }
            }
            QMetaObject::invokeMethod(
                this,
                [this, gen, alive, entries]() mutable
                {
                    // Always drop the busy cursor, even if superseded/destroyed.
                    QApplication::restoreOverrideCursor();
                    if (!alive || !*alive)
                        return;
                    if (gen != m_dirGen) // a newer folder superseded this scan
                        return;
                    m_allEntries = entries;
                    m_metaIndex.clear();
                    applyFilter();
                    // Only pay the header-read cost when the Details view
                    // actually shows the resolution column.
                    if (m_viewMode == Details)
                        ensureDimensions();
                },
                Qt::QueuedConnection);
        })
        .detach();
}

void ThumbnailPanel::refresh()
{
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::ensureDimensions()
{
    if (m_dimsResolved || m_allEntries.isEmpty())
        return;
    m_dimsResolved = true; // mark up-front so we launch the worker only once

    const int gen = m_dirGen;
    QStringList paths;
    paths.reserve(m_allEntries.size());
    for (const Entry &e : m_allEntries)
        paths.append(e.path);

    auto alive = m_alive;
    std::thread(
        [this, gen, paths, alive]()
        {
            QVector<QSize> sizes;
            sizes.reserve(paths.size());
            for (const QString &p : paths)
            {
                QImageReader reader(p);
                reader.setAutoTransform(true);
                sizes.append(reader.size());
            }
            QMetaObject::invokeMethod(
                this,
                [this, gen, sizes, alive]()
                {
                    if (!alive || !*alive)
                        return;
                    if (gen != m_dirGen) // folder changed while resolving
                        return;
                    for (int i = 0; i < sizes.size() && i < m_allEntries.size(); ++i)
                    {
                        m_allEntries[i].width = sizes[i].width();
                        m_allEntries[i].height = sizes[i].height();
                    }
                    viewport()->update();
                },
                Qt::QueuedConnection);
        })
        .detach();
}

// M23 re-check: setViewMode now lives in thumbnailpanel_viewmode.cpp to keep
// this TU under the 800-line guard.

void ThumbnailPanel::setThumbSize(int size)
{
    size = qBound(kMinThumbSize, size, kMaxThumbSize);
    if (size == m_thumbSize)
        return;
    m_thumbSize = size;
    // Update pipeline to generate thumbnails at the new size.
    ThumbnailPipeline::instance().thumbSize = size;
    // Directly update gridSize instead of calling setViewMode(m_viewMode),
    // because setViewMode early-returns when the mode hasn't changed.
    if (m_viewMode == ViewMode::Compact)
    {
        const int compactS = qMax(m_thumbSize / 3, 32);
        setGridSize(QSize(compactS + 4, compactS + 14));
    }
    else
    {
        setGridSize(QSize(m_thumbSize + 16, m_thumbSize + 34));
    }
    viewport()->update();
}

void ThumbnailPanel::buildModel(const QList<Entry> &entries)
{
    // Preserve selection and current index across model rebuild (e.g. when
    // sorting changes).  Without this, setStringList() resets the entire
    // selection model and the user's multi-select is silently lost.
    const QStringList prevSelected = selectedPaths();
    const QString prevCurrent =
        m_paths.isEmpty()
            ? QString()
            : (currentIndex().isValid() ? m_paths.value(currentIndex().row()) : QString());

    m_paths.clear();
    m_rowByPath.clear();
    m_sizeByPath.clear();
    QStringList names;
    names.reserve(entries.size());
    qint64 total = 0;
    for (int i = 0; i < entries.size(); ++i)
    {
        m_paths.append(entries.at(i).path);
        m_rowByPath.insert(entries.at(i).path, i);
        m_sizeByPath.insert(entries.at(i).path, entries.at(i).size);
        names.append(entries.at(i).name);
        total += entries.at(i).size;
    }
    m_totalBytes = total;
    m_model->setStringList(names);

    // Restore selection and current index.
    QItemSelection selToRestore;
    for (const QString &p : prevSelected)
    {
        auto it = m_rowByPath.constFind(p);
        if (it != m_rowByPath.constEnd())
            selToRestore.select(m_model->index(it.value(), 0), m_model->index(it.value(), 0));
    }
    if (!selToRestore.isEmpty())
        selectionModel()->select(selToRestore, QItemSelectionModel::ClearAndSelect);
    if (!prevCurrent.isEmpty())
    {
        auto it = m_rowByPath.constFind(prevCurrent);
        if (it != m_rowByPath.constEnd())
            setCurrentIndex(m_model->index(it.value(), 0));
    }

    {
        QMutexLocker lk(&m_thumbMtx);
        m_thumbReady.clear();
        m_thumbPending.clear();
        m_thumbFailed.clear();
    }
    ThumbnailPipeline::instance().setSources(toStdPaths(m_paths));

    emit statsChanged(m_paths.size(), m_totalBytes, 0, 0);
    // Defer priority scheduling until layout/geometry is ready (avoids
    // scheduling the whole directory before the viewport is laid out).
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::updateVisibleRange()
{
    const int n = m_model->rowCount();
    if (n == 0)
        return;
    // Geometry may not be ready yet (panel not shown / zero-size). Wait for
    // showEvent / resizeEvent before scheduling.
    if (viewport()->width() < 10 || viewport()->height() < 10)
        return;

    const QModelIndex firstIdx = indexAt(QPoint(2, 2));
    const QModelIndex lastIdx = indexAt(QPoint(viewport()->width() - 2, viewport()->height() - 2));
    int first = firstIdx.isValid() ? firstIdx.row() : 0;
    int last = lastIdx.isValid() ? lastIdx.row() : n - 1;
    if (last < first)
        last = first;

    // Prime instantly-available disk-cached thumbs for the visible window so a
    // revisited folder paints at once instead of waiting for a decode.
    {
        QMutexLocker lk(&m_thumbMtx);
        for (int r = first; r <= last && r < n; ++r)
        {
            const QString &p = m_paths.at(r);
            if (m_thumbReady.contains(p))
                continue;
            QPixmap pm;
            if (ThumbnailCache::instance().get(p, pm))
            {
                m_thumbReady.insert(p, pm);
                QPointer<ThumbnailPanel> guard(this);
                QMetaObject::invokeMethod(
                    this,
                    [guard, p]()
                    {
                        if (!guard)
                            return;
                        guard->onThumbReady(p);
                    },
                    Qt::QueuedConnection);
            }
        }
    }

    // P0 #② / A-2.5: visible range at Thumbnail priority, then predictive
    // neighbors. Scale the predictive window with directory size so 10k-image
    // folders still feel snappy when scrolling, without over-scheduling tiny
    // folders.
    ThumbnailPipeline::instance().setVisibleRange(static_cast<size_t>(first),
                                                  static_cast<size_t>(last + 1));
    const int predictive = n > 2000 ? 96 : (n > 500 ? 64 : 48);
    ThumbnailPipeline::instance().setPredictiveCount(static_cast<size_t>(predictive));
}

void ThumbnailPanel::onThumbReady(const QString &path)
{
    const int row = m_rowByPath.value(path, -1);
    if (row < 0)
        return;
    const QModelIndex idx = m_model->index(row, 0);
    emit dataChanged(idx, idx, {Qt::DecorationRole});
}

QPixmap ThumbnailPanel::thumbReady(const QString &path) const
{
    QMutexLocker lk(&m_thumbMtx);
    auto it = m_thumbReady.constFind(path);
    return it == m_thumbReady.constEnd() ? QPixmap() : it.value();
}

bool ThumbnailPanel::thumbFailed(const QString &path) const
{
    QMutexLocker lk(&m_thumbMtx);
    return m_thumbFailed.contains(path);
}

void ThumbnailPanel::onSelectionChanged()
{
    const QModelIndexList sel = selectionModel()->selectedIndexes();
    qint64 selBytes = 0;
    for (const QModelIndex &idx : sel)
        selBytes += m_sizeByPath.value(m_paths.value(idx.row()), 0);
    const int n = sel.size();

    // M23 P2 (selection UX): keep the compare affordance always discoverable.
    // It shows the live selection count, enables once 2–8 images are picked,
    // and is hidden only when nothing is selected.
    if (n == 0 || m_currentDir.isEmpty())
    {
        m_compareBtn->setVisible(false);
    }
    else
    {
        m_compareBtn->setVisible(true);
        m_compareBtn->setText(QString("比较选中 (%1)").arg(n));
        const bool canCompare = n >= 2 && n <= 8;
        m_compareBtn->setEnabled(canCompare);
        m_compareBtn->setToolTip(canCompare
                                     ? QString("将选中的 %1 张图片送入对比").arg(n)
                                     : QString("需要选择 2–8 张图片才能对比（当前 %1 张）").arg(n));
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
        return;
    const QModelIndex idx = m_model->index(row, 0);
    if (currentIndex() == idx && selectionModel() && selectionModel()->isSelected(idx))
        return; // already the current selected item — nothing to do, no scroll jank
    // If the path is already part of a multi-selection, only move the current
    // focus — do NOT ClearAndSelect (that would collapse multi-select).
    if (selectionModel() && selectionModel()->isSelected(idx))
    {
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
        scrollTo(idx);
        return;
    }
    // Single-path focus: replace selection with this item.
    if (selectionModel())
        selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
    else
        setCurrentIndex(idx);
    scrollTo(idx); // default EnsureVisible: only scrolls when off-screen
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
    // Block currentChanged → itemClicked while we rebuild multi-select, so the
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
        }
    }
    selectionModel()->blockSignals(wasBlocked);
    // Emit a single selectionChanged so MainWindow can re-sync if needed.
    emit selectionModel() -> selectionChanged(selectionModel()->selection(), QItemSelection());
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

void ThumbnailPanel::resizeEvent(QResizeEvent *event)
{
    QListView::resizeEvent(event);
    if (m_compareBtn)
        m_compareBtn->move(viewport()->width() - m_compareBtn->width() - 8, 8);
    positionDetailsHeader();
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::wheelEvent(QWheelEvent *event)
{
    // Ctrl+wheel resizes thumbnails (Windows Explorer / FastStone parity);
    // plain wheel scrolls as usual.
    if (event->modifiers() & Qt::ControlModifier)
    {
        const int delta = event->angleDelta().y();
        if (delta != 0)
        {
            const int step = (delta > 0 ? 1 : -1) * 16;
            setThumbSize(qBound(kMinThumbSize, m_thumbSize + step, kMaxThumbSize));
            event->accept();
            return;
        }
    }
    QListView::wheelEvent(event);
}

void ThumbnailPanel::dragEnterEvent(QDragEnterEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QListView::dragEnterEvent(event);
}

void ThumbnailPanel::dragMoveEvent(QDragMoveEvent *event)
{
    if (event->mimeData()->hasUrls())
        event->acceptProposedAction();
    else
        QListView::dragMoveEvent(event);
}

void ThumbnailPanel::dropEvent(QDropEvent *event)
{
    if (!event->mimeData()->hasUrls())
    {
        QListView::dropEvent(event);
        return;
    }
    QStringList paths;
    for (const QUrl &url : event->mimeData()->urls())
    {
        const QString local = url.toLocalFile();
        if (!local.isEmpty())
            paths.append(local);
    }
    if (paths.isEmpty())
    {
        QListView::dropEvent(event);
        return;
    }
    event->acceptProposedAction();
    emit filesDropped(paths);
}

void ThumbnailPanel::positionDetailsHeader()
{
    if (!m_detailsHeader || m_viewMode != Details)
        return;
    const QRect vp = viewport()->geometry();
    m_detailsHeader->setGeometry(vp.x(), vp.y() - kDetailsHeaderH, vp.width(), kDetailsHeaderH);
    m_detailsHeader->raise();
}

void ThumbnailPanel::showEvent(QShowEvent *event)
{
    QListView::showEvent(event);
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::mousePressEvent(QMouseEvent *event)
{
    // P0: Enforce single-selection on plain left-click (no modifier keys).
    // ExtendedSelection normally handles this, but in IconMode with certain Qt
    // builds the selection is not reliably cleared. We make it explicit.
    if (event->button() == Qt::LeftButton &&
        !(event->modifiers() & (Qt::ControlModifier | Qt::ShiftModifier)))
    {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid())
        {
            // CRITICAL FIX: Call setCurrentIndex to ensure currentChanged signal fires.
            // Without this, itemClicked won't be emitted and the preview panel
            // won't refresh when clicking a single image.
            selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
            event->accept();
            return;
        }
        // Click on empty area: deselect everything.
        selectionModel()->clearSelection();
        event->accept();
        return;
    }
    QListView::mousePressEvent(event);
}

void ThumbnailPanel::stopThumbnailWorker()
{
    QMutexLocker lk(&m_thumbMtx);
    m_thumbReady.clear();
    m_thumbPending.clear();
    m_thumbFailed.clear();
}
