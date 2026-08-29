#include "selectionmodel.h"
#include "thumbnailpanel_p.h"
#include "thumbnailprovider.h"

// P0#3: DetailsHeader (the column-title strip above the Details list) is now
// defined in thumbnailpanel_p.h alongside the shared DetailLayout geometry, so
// it stays in sync with the delegate cells and keeps this TU lean.

// ---- ThumbnailPanel ---------------------------------------------------------

ThumbnailPanel::ThumbnailPanel(QWidget *parent) : QListView(parent)
{
    m_liveDirectoryMonitoring = false;
    m_scanGenToken = std::make_shared<std::atomic<uint64_t>>(0);
    m_busyCursorRefs = std::make_shared<std::atomic<int>>(0);
    // M24: bounded background workers (directory scan + dimension resolve).
    // Two threads keep first-screen scans fast without unbounded growth when
    // the user switches folders rapidly.
    m_scanPool.setMaxThreadCount(2);

    // QListView:: prefix disambiguates from our own ThumbnailPanel::setViewMode().
    QListView::setViewMode(QListView::IconMode);
    setMovement(QListView::Static);
    setResizeMode(QListView::Adjust);
    // M24 (S3): a 10000-row directory insert must not stall the UI thread for
    // seconds with one synchronous Adjust layout. Batched mode processes the
    // layout in chunks through the event loop, keeping the UI responsive while
    // entries stream in.
    setLayoutMode(QListView::Batched);
    setBatchSize(512);
    setWrapping(true);
    setUniformItemSizes(true); // all cells identical -> cheap layout for huge lists
    setSpacing(6);
    setGridSize(QSize(m_thumbSize + 24, m_thumbSize + 62));
    setSelectionMode(QAbstractItemView::ExtendedSelection);
    setTextElideMode(Qt::ElideRight);
    setEditTriggers(QAbstractItemView::NoEditTriggers);
    setVerticalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollMode(QAbstractItemView::ScrollPerPixel);
    setHorizontalScrollBarPolicy(Qt::ScrollBarAlwaysOff);
    viewport()->setAttribute(Qt::WA_Hover);
    viewport()->setMouseTracking(true);

    m_model = new QStringListModel(this);
    setModel(m_model);

    m_delegate = new ThumbDelegate(this, this);
    setItemDelegate(m_delegate);

    m_compareBtn = new QPushButton(QStringLiteral("比较选中"), this);
    m_compareBtn->setObjectName(QStringLiteral("compareSelectionButton"));
    m_compareBtn->setVisible(false);
    connect(m_compareBtn, &QPushButton::clicked, this, &ThumbnailPanel::onCompareClicked);

    connect(selectionModel(), &QItemSelectionModel::selectionChanged, this,
            &ThumbnailPanel::onSelectionChanged);

    // P0-2: publish gallery hover to the app-wide SelectionModel (`hovered`).
    connect(this, &QAbstractItemView::entered, this,
            [this](const QModelIndex &idx)
            {
                if (!idx.isValid() || !m_selection)
                    return;
                const QString p = m_paths.value(idx.row());
                if (p.isEmpty())
                    return;
                m_selection->setHovered(p);
                emit hovered(p);
            });

    // Drive thumbnail decode priority from the viewport (P0 priority).
    connect(verticalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailPanel::updateVisibleRange);
    connect(horizontalScrollBar(), &QScrollBar::valueChanged, this,
            &ThumbnailPanel::updateVisibleRange);

    // Restore path-based navigation signals (used by MainWindow to open images
    // and refresh the metadata panel) from the view's built-in index signals.
    connect(this, &QAbstractItemView::clicked, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid() && !m_selectionGesture)
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
    // SelectionModel no-ops on a same-path set, so selectPath() -> currentChanged
    // -> itemClicked cannot loop.
    connect(this, &QAbstractItemView::activated, this,
            [this](const QModelIndex &idx)
            {
                if (idx.isValid())
                    emit itemDoubleClicked(m_paths.value(idx.row()));
            });
    connect(selectionModel(), &QItemSelectionModel::currentChanged, this,
            [this](const QModelIndex &current, const QModelIndex &)
            {
                if (current.isValid() && !m_selectionGesture)
                    emit itemClicked(m_paths.value(current.row()));
            });

    wireThumbnailPipeline();
}

void ThumbnailPanel::wireThumbnailPipeline()
{
    // Wire the shared pipeline once. Worker callbacks only marshal value data
    // back to the owning panel's GUI thread.
    m_alive = std::make_shared<std::atomic<bool>>(true);
    if (m_pipelineWired)
        return;
    m_pipelineWired = true;
    ThumbnailPipeline::instance().thumbSize = m_thumbSize;
    ThumbnailPipeline::instance().setDecodeFn(
        [](const std::string &p, int size) { return ThumbnailProvider::produce(p, size); });
    auto alive = m_alive;
    const QPointer<ThumbnailPanel> self(this);
    ThumbnailPipeline::instance().setResultFn(
        [alive, self](const std::string &p, int size, const ImageData &img)
        {
            if (!alive->load() || !self)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [alive, self, p, size, img]()
                {
                    if (!alive->load() || !self)
                        return;
                    ThumbnailPanel *panel = self.data();
                    const QString qp = QString::fromStdString(p);
                    if (size != panel->m_thumbSize)
                        return;
                    const QString cacheKey = panel->thumbCacheKey(qp, size);
                    if (img.isNull())
                    {
                        QMutexLocker l(&panel->m_thumbMtx);
                        panel->m_thumbFailed.insert(cacheKey);
                        panel->m_thumbPending.remove(cacheKey);
                        panel->onThumbReady(qp);
                        return;
                    }
                    const QImage q = mvcore::toQImage(img);
                    if (q.isNull())
                    {
                        QMutexLocker l(&panel->m_thumbMtx);
                        panel->m_thumbFailed.insert(cacheKey);
                        panel->m_thumbPending.remove(cacheKey);
                        panel->onThumbReady(qp);
                        return;
                    }
                    {
                        QMutexLocker lk(&panel->m_thumbMtx);
                        auto old = panel->m_thumbReady.find(cacheKey);
                        if (old != panel->m_thumbReady.end())
                            panel->m_thumbReadyBytes =
                                qMax<qint64>(0, panel->m_thumbReadyBytes - old->bytes);
                        ThumbnailPanel::ReadyPixmap ready;
                        ready.pixmap = QPixmap::fromImage(q);
                        ready.bytes = static_cast<qint64>(q.sizeInBytes());
                        ready.lastUse = ++panel->m_thumbReadyClock;
                        panel->m_thumbReady.insert(cacheKey, std::move(ready));
                        panel->m_thumbReadyBytes += panel->m_thumbReady.find(cacheKey)->bytes;
                        panel->m_thumbFailed.remove(cacheKey);
                        panel->m_thumbPending.remove(cacheKey);
                        panel->enforceThumbPixmapBudgetLocked();
                    }
                    panel->onThumbReady(qp);
                });
        });
}

ThumbnailPanel::~ThumbnailPanel()
{
    if (m_batchTask)
        TaskScheduler::cancel(m_batchTask);
    if (m_fileOperationTask)
        TaskScheduler::cancel(m_fileOperationTask);
    if (m_batchProgress)
        m_batchProgress->close();
    if (m_fileProgress)
        m_fileProgress->close();
    ++m_fileOperationGeneration;
    if (m_alive)
        *m_alive = false;
    // M24: drop queued scan/dimension tasks; in-flight ones abort at their next
    // m_alive check, so destruction waits only a bounded time for the current
    // QDir sort (typically < 100 ms even for 10k-image folders).
    m_scanPool.clear();
    // M46: queued scans dropped by clear() never run their completion, so
    // their busy-cursor refs would leak. Drain them here; a scan that was
    // still RUNNING marshals its own restore later, which becomes a no-op
    // because the refcount is already zero - the cursor stays balanced in
    // every interleaving.
    while (m_busyCursorRefs && m_busyCursorRefs->load(std::memory_order_acquire) > 0)
        restoreBusyCursorOnce(m_busyCursorRefs);
    // Detach from the shared pipeline so its worker thread can't call back into
    // a destroyed panel (and restore the default decode for the next panel).
    ThumbnailPipeline::instance().setResultFn([](const std::string &, int, const ImageData &) {});
    ThumbnailPipeline::instance().setDecodeFn([](const std::string &p, int size)
                                              { return Decoder::decodeScaled(p, size); });
}



void ThumbnailPanel::resetDirectoryState()
{
    m_allEntries.clear();
    m_paths.clear();
    m_rowByPath.clear();
    m_sourceRowByPath.clear();
    m_sizeByPath.clear();
    m_displayEntries.clear();
    m_displayEntryRow.clear();
    m_metaIndex.clear();
    m_metaIso.clear();
    m_metaCamera.clear();
    m_metaLens.clear();
    m_metaIndexing = false;
    m_recursiveSearching = false;
    m_recursiveHits.clear();
    m_recursiveHitsFor.clear();
    {
        QMutexLocker lk(&m_thumbMtx);
        m_thumbReady.clear();
        m_thumbReadyBytes = 0;
        m_thumbReadyClock = 0;
        m_thumbPending.clear();
        m_thumbFailed.clear();
        m_thumbDirtyPaths.clear();
    }
}

void ThumbnailPanel::refresh()
{
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}


// M23 re-check: setViewMode now lives in thumbnailpanel_viewmode.cpp to keep
// this TU under the 800-line guard.

void ThumbnailPanel::setThumbSize(int size)
{
    size = qBound(kMinThumbSize, size, kMaxThumbSize);
    if (m_viewMode == ViewMode::LargeIcon || m_viewMode == ViewMode::SmallIcon)
    {
        // Large/Small are product presets. Session/preferences may call this
        // after setViewMode(), so remember the requested grid size but keep the
        // visible preset and its slider value authoritative.
        m_gridThumbSize = size;
        emit thumbSizeChanged(m_thumbSize);
        return;
    }
    applyThumbSize(size, true);
}

void ThumbnailPanel::applyThumbSize(int size, bool rememberGridSize)
{
    size = qBound(kMinThumbSize, size, kMaxThumbSize);
    if (size == m_thumbSize)
    {
        if (rememberGridSize)
            m_gridThumbSize = size;
        emit thumbSizeChanged(m_thumbSize);
        return;
    }
    m_thumbSize = size;
    if (rememberGridSize)
        m_gridThumbSize = size;
    // M46: the pipeline treats a size change as a supersession boundary - it
    // cancels in-flight old-size decodes and drops the old-size memory cache
    // and pending keys, so a slider drag cannot keep an unbounded old-size
    // workload running (previously only the ready map here was cleared and
    // old-size decodes kept decoding to completion).
    ThumbnailPipeline::instance().setThumbSize(size);
    // M25: the ready cache and in-flight pending set belong to the OLD size -
    // a stale 64px pixmap must never masquerade as the new 240px cell, and an
    // old-size result still in flight must not land in the ready map (the
    // result callback re-checks the size, so dropping the pending marker here
    // is enough). Failed entries are size-independent and stay.
    {
        QMutexLocker lk(&m_thumbMtx);
        m_thumbReady.clear();
        m_thumbReadyBytes = 0;
        m_thumbReadyClock = 0;
        m_thumbPending.clear();
        m_thumbFailed.clear();
    }
    // Directly update gridSize instead of calling setViewMode(m_viewMode),
    // because setViewMode early-returns when the mode hasn't changed.
    if (m_viewMode == ViewMode::Thumbnail || m_viewMode == ViewMode::LargeIcon)
    {
        setGridSize(QSize(m_thumbSize + 24, m_thumbSize + 62));
    }
    else if (m_viewMode == ViewMode::SmallIcon)
    {
        setGridSize(QSize(m_thumbSize + 12, m_thumbSize + 30));
    }
    else if (m_viewMode == ViewMode::Filmstrip)
    {
        const int stripH = qMax(m_thumbSize, 64) + 18;
        setGridSize(QSize(stripH, stripH));
    }
    else if (m_viewMode == ViewMode::Compact)
    {
        const int compactS = qMax(m_thumbSize / 3, 32);
        setGridSize(QSize(compactS + 4, compactS + 14));
    }
    else if (m_viewMode != ViewMode::Details && m_viewMode != ViewMode::List)
    {
        setGridSize(QSize(m_thumbSize + 24, m_thumbSize + 62));
    }
    viewport()->update();
    // M25: the visible cells must be re-requested at the new size. The ready
    // cache was just cleared, so this reschedules the visible window (and its
    // predictive neighbors) through the pipeline at the new identity.
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
    emit thumbSizeChanged(m_thumbSize);
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
    const bool hadGallerySelection = !prevSelected.isEmpty();
    const bool pendingSelectionBeforeRebuild = !m_pendingSelect.isEmpty();

    m_paths.clear();
    m_rowByPath.clear();
    m_sizeByPath.clear();
    QStringList names;
    names.reserve(entries.size());
    qint64 total = 0;
    // M46: the display-entry index is the delegates' paint-time data source.
    // It mirrors EXACTLY what buildModel shows (including recursive-search
    // hits), so size/date never need a filesystem query at paint time.
    m_displayEntries = entries;
    m_displayEntryRow.clear();
    m_displayEntryRow.reserve(static_cast<int>(entries.size()));
    for (int i = 0; i < entries.size(); ++i)
    {
        m_paths.append(entries.at(i).path);
        m_rowByPath.insert(entries.at(i).path, i);
        m_displayEntryRow.insert(entries.at(i).path, i);
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

    // A completed filter rebuild with no visible rows must clear the shared
    // selection as well as the gallery. This drives the existing preview and
    // status-bar empty state through SelectionModel; recursive/meta searches
    // that are still pending never reach buildModel().
    if (entries.isEmpty() && m_selection)
        m_selection->setSelection({}, {});

    pruneThumbnailState();
    if (m_scanProgressive)
        ThumbnailPipeline::instance().replaceSources(toStdPaths(m_paths));
    else
        ThumbnailPipeline::instance().setSources(toStdPaths(m_paths));
    // M37: publish the exact order displayed by the gallery. Sort/filter
    // rebuilds also pass through here, keeping navigation and preload aligned.
    emit sequenceChanged(m_currentDir, m_paths);

    // M24 (A#8): apply a selection that was requested while this model was
    // still being rebuilt (e.g. rename -> async rescan -> re-select new name).
    if (!m_pendingSelect.isEmpty() && m_rowByPath.contains(m_pendingSelect))
    {
        selectPath(m_pendingSelect);
        m_pendingSelect.clear();
    }
    else if (hadGallerySelection && selToRestore.isEmpty() && !entries.isEmpty() &&
             !pendingSelectionBeforeRebuild && m_pendingSelect.isEmpty())
    {
        // A filter may remove the whole previous selection while leaving
        // visible rows. Promote exactly one visible path so Browse, preview,
        // and the shared SelectionModel never drift into an unselected state.
        selectPath(m_paths.first());
    }

    emit statsChanged(m_paths.size(), m_totalBytes, 0, 0);
    // Defer priority scheduling until layout/geometry is ready (avoids
    // scheduling the whole directory before the viewport is laid out).
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}

void ThumbnailPanel::setSelectionModel(SelectionModel *sel)
{
    m_selection = sel;
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
    const bool left = event->button() == Qt::LeftButton;
    const Qt::KeyboardModifiers mods = event->modifiers();
    m_selectionGesture = left && (mods & (Qt::ControlModifier | Qt::ShiftModifier));

    // Keep the native QListView gesture surface, but apply the selection
    // command explicitly. In IconMode on Windows, QListView can retain only
    // the clicked item for Shift ranges when the previous click was a custom
    // ClearAndSelect; the path anchor makes plain/Ctrl/Shift deterministic.
    if (left)
    {
        const QModelIndex idx = indexAt(event->pos());
        if (idx.isValid())
        {
            const QString path = m_paths.value(idx.row());
            if (mods & Qt::ShiftModifier)
            {
                int anchorRow = m_rowByPath.value(m_selectionAnchorPath, -1);
                if (anchorRow < 0 && currentIndex().isValid())
                    anchorRow = currentIndex().row();
                if (anchorRow < 0)
                    anchorRow = idx.row();
                const int first = qMin(anchorRow, idx.row());
                const int last = qMax(anchorRow, idx.row());
                const QModelIndex firstIndex = m_model->index(first, 0);
                const QModelIndex lastIndex = m_model->index(last, 0);
                selectionModel()->select(QItemSelection(firstIndex, lastIndex),
                                         QItemSelectionModel::Select);
                selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
            }
            else if (mods & Qt::ControlModifier)
            {
                selectionModel()->select(idx, QItemSelectionModel::Toggle);
                selectionModel()->setCurrentIndex(idx, QItemSelectionModel::NoUpdate);
                m_selectionAnchorPath = path;
            }
            else
            {
                selectionModel()->setCurrentIndex(idx, QItemSelectionModel::ClearAndSelect);
                m_selectionAnchorPath = path;
            }
            event->accept();
            return;
        }
        // Click on empty area: deselect everything.
        selectionModel()->clearSelection();
        m_selectionAnchorPath.clear();
        event->accept();
        return;
    }
    QListView::mousePressEvent(event);
}

void ThumbnailPanel::mouseReleaseEvent(QMouseEvent *event)
{
    if (event->button() == Qt::LeftButton && m_selectionGesture)
    {
        m_selectionGesture = false;
        event->accept();
        return;
    }
    QListView::mouseReleaseEvent(event);
    if (event->button() == Qt::LeftButton)
        m_selectionGesture = false;
}

void ThumbnailPanel::stopThumbnailWorker()
{
    QMutexLocker lk(&m_thumbMtx);
    m_thumbReady.clear();
    m_thumbReadyBytes = 0;
    m_thumbReadyClock = 0;
    m_thumbPending.clear();
    m_thumbFailed.clear();
    m_thumbDirtyPaths.clear();
}
