#pragma once

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

#include <QDate>
#include <QDateTime>
#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QListView>
#include <QMouseEvent>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QShowEvent>
#include <QSize>
#include <QStringList>
#include <QStyledItemDelegate>
#include <QThreadPool>
#include <QVector>

#include "core/RatingStore.h"
#include "core/TagStore.h"
#include "core/filesystem/DirectorySnapshot.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/search/BrowseQuery.h"

class QPushButton;
class QContextMenuEvent;
class QResizeEvent;
class QStringListModel;
class CommandStack;
class ICommand;
class SelectionModel;
class QProgressDialog;
class QRegularExpression;

// Virtualized thumbnail gallery (P0 #①/#②).
//
// Unlike the old QListWidget design (one widget + one decode task per image,
// hard-capped at 1000), this holds only a path list — no per-image widget — so
// it scrolls smoothly with tens of thousands of images. Visible cells are
// decoded on demand through the shared ThumbnailPipeline (viewport + predictive
// priority), which already owns the LRU / disk-cache / scheduler machinery.
class ThumbnailPanel : public QListView
{
    Q_OBJECT

  public:
    static constexpr int kDefaultThumbSize = 140;
    static constexpr int kMinThumbSize = 48;
    static constexpr int kMaxThumbSize = 512;
    // M55: the UI owns a bounded display cache in addition to the core
    // ImageData LRU. The count keeps a generous visible+halo working set while
    // the byte cap protects large thumbnail sizes from driving RSS linearly.
    static constexpr int kThumbPixmapCacheMaxEntries = 384;
    static constexpr qint64 kThumbPixmapCacheMaxBytes = 96LL * 1024 * 1024;

    enum SortMode
    {
        SortName,
        SortDate,
        SortSize,
        SortResolution,
        SortType,   // A-2.2: sort by file extension
        SortRating, // A-2.2: sort by star rating
        SortCamera, // P0 #①: sort by camera make+model
        SortLens    // P0 #①: sort by lens model
    };

    enum ViewMode
    {
        Thumbnail = 0, // Grid of thumbnails (default)
        LargeIcon,     // P0-2: big thumbnail grid
        SmallIcon,     // P0-2: small thumbnail grid
        Details,       // List view with columns
        Filmstrip,     // Horizontal strip, single row (M15)
        Compact,       // Dense grid, minimal padding (M15)
        List           // Windows-Explorer-style icon+name wrapping list (P0)
    };

    explicit ThumbnailPanel(QWidget *parent = nullptr);
    ~ThumbnailPanel() override;

    void setDirectory(const QString &path);
    // M56: notify the host that this panel now has a committed source path.
    // This is an observation boundary for the monitor, not a navigation event.
    void setLiveDirectoryMonitoring(bool enabled)
    {
        m_liveDirectoryMonitoring = enabled;
    }
    // M56: apply an already-reconciled filesystem delta without crossing the
    // directory navigation boundary or resetting the whole gallery model.
    void applyDirectoryDelta(const mviewer::core::DirectoryDelta &delta);
    // M56: re-evaluate the active filter after a sidecar import. Source rows
    // stay intact and any membership change is row-local.
    void refreshSidecarPaths(const QStringList &paths);
    // P0-1: F5 refresh — reload the current directory from disk.
    void refresh();
    QString currentDir() const
    {
        return m_currentDir;
    }
    // P0-2: programmatically make `path` the selected/current item so the grid
    // highlight stays in lock-step with the shared SelectionModel. Unlike
    // scrollToPath() this only scrolls when the item is off-screen (avoids
    // recentring jank when the user clicked an already-visible thumbnail).
    void selectPath(const QString &path);
    // M19: apply a multi-selection from SelectionModel onto the gallery.
    // `current` becomes the focused item; empty current falls back to paths[0].
    void selectPaths(const QStringList &paths, const QString &current = {});
    // P0-2: inject the app-wide SelectionModel so hover events publish `hovered`.
    void setSelectionModel(SelectionModel *sel);
    void setSortMode(SortMode mode);
    // A-2.2: toggle ascending/descending sort order.
    void setSortAscending(bool ascending);
    bool sortAscending() const
    {
        return m_sortAscending;
    }
    // A-2.3: filter by file type (e.g. "jpg", "png", "raw"). Empty = all types.
    // Multiple types can be OR-ed via comma: "jpg,png,tiff".
    void setTypeFilter(const QString &types);
    QString typeFilter() const
    {
        return m_typeFilter;
    }
    void setViewMode(ViewMode mode);
    ViewMode viewMode() const
    {
        return m_viewMode;
    }

    // M15: dynamic thumbnail size (slider-controlled).
    void setThumbSize(int size);
    int thumbSize() const
    {
        return m_thumbSize;
    }

    // Entry metadata — public so DetailsDelegate can read it.
    struct Entry
    {
        QString path;
        QString name;
        qint64 size = 0;
        int width = 0;
        int height = 0;
        QDateTime date;
        int frameCount = 1;
        bool animated = false;
    };

    // M18: live search. Filters the gallery by filename (case-insensitive
    // substring). When `recursive` is true, subfolders are enumerated and any
    // matching image is appended as a temporary item (cleared on next
    // setDirectory). Empty `text` clears the filter.
    void setFilter(const QString &text, bool recursive = false);

    // P1: metadata-aware search (camera / lens / ISO / date / …) and star-rating
    // filter. Both refine the same filtered view produced by setFilter().
    void setMetaSearch(bool on);
    void setRatingFilter(int stars);

    // P3 tail: color label / reject / pick / recents filters (each independent;
    // applyFilter() combines them with AND). 0 label = any; recent uses the
    // RatingStore recents list.
    void setLabelFilter(int label);
    void setRejectFilter(bool on);
    void setPickFilter(bool on);
    void setRecentFilter(bool on);
    void clearFlagFilters();

    // P0 #①: metadata filters. Camera/Lens match as case-insensitive substrings
    // against the EXIF "make model lens" index; ISO matches the exact numeric
    // value. These combine with the other filters via AND in applyFilter().
    void setCameraFilter(const QString &camera);
    void setLensFilter(const QString &lens);
    void setIsoFilter(int iso);
    // P0 #①: free-form tag filter (matches images carrying this exact tag).
    void setTagFilter(const QString &tag);

    // Quiesce background decode work (e.g. before a headless render where async
    // QPixmap updates are undesirable). Public so test/demo harnesses can
    // quiesce the panel.
    void stopThumbnailWorker();

    // M46 deterministic-test instrumentation: called on the worker at every
    // scan/dimension iteration. Empty in production; tests install it to prove
    // a superseded scan aborts after a bounded number of iterations.
    static void setScanIterationProbe(const std::function<void()> &probe);
    // M46 test observability: the shared scan-generation token. Tests read it
    // from the iteration probe to attribute iterations to a directory
    // generation. Production code never needs it.
    std::shared_ptr<std::atomic<uint64_t>> scanGenTokenForTest() const
    {
        return m_scanGenToken;
    }

    // Paths of the currently selected gallery rows (in model order). Public:
    // MainWindow and acceptance tests consume the selection.
    QStringList selectedPaths() const;

  private:
    // M46 async-scan internals (defined in thumbnailpanel_async.cpp):
    // exception-safe probe invocation; UI-side busy-cursor refcount helpers
    // (restoreBusyCursorOnce must run on the GUI thread).
    static void invokeScanProbe();
    static std::shared_ptr<const std::function<void()>> scanIterationProbeSnapshot();
    static void restoreBusyCursorOnce(const std::shared_ptr<std::atomic<int>> &refs);
    static void marshalBusyRestore(const std::shared_ptr<std::atomic<int>> &refs);
    void startDirectoryScan(const QString &path, int gen, const QString &typeFilter,
                            SortMode sortMode, bool sortAscending,
                            const std::shared_ptr<std::atomic<bool>> &alive,
                            const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                            const std::shared_ptr<std::atomic<int>> &busyRefs,
                            const QPointer<ThumbnailPanel> &self);
    // M54: publish scan batches before the final sorted convergence pass.
    void applyScanBatch(int gen, const QList<Entry> &batch);
    // M46: publish a completed scan's entries on the UI thread (extracted from
    // setDirectory's completion lambda so the scanning TU stays under the
    // function-length gate). Runs on the GUI thread; re-checks the generation.
    void applyScanResult(int gen, const QList<Entry> &entries);
    // Keep decoded thumbnail state for the current directory across
    // filter/sort rebuilds; implementation lives with pipeline delivery.
    void pruneThumbnailState();

  public:
    // Scroll the grid so the item for `path` is visible and select it. Used by
    // browse-position restore (reopen last image after launch).
    void scrollToPath(const QString &path);
    // Current vertical scroll offset of the thumbnail grid (for persistence).
    int scrollOffset() const;

    void renameSelected();
    void moveToTrashSelected();
    void copySelectedTo();
    void moveSelectedTo();
    void revealSelected();

    // A-10: reversible file ops via CommandStack. When set, rename/delete/move
    // go through the stack so Ctrl+Z can reverse them. When null, falls back
    // to the irreversible direct path (tests / headless).
    void setCommandStack(class CommandStack *stack)
    {
        m_cmdStack = stack;
    }

    // M13.4: run a chosen analyzer over every selected image and export the
    // structured per-image metrics to CSV/JSON. Drives core AnalyzerRegistry.
    void batchAnalyzeExport();

    // P0 #①: read access for the delegate (paths + ready pixmaps + entry data).
    const QStringList &pathList() const
    {
        return m_paths;
    }
    // M17: paths currently visible in the gallery (post-filter). Same as pathList()
    // when a filter is active — added for explicit "export filtered set" UX.
    const QStringList &visiblePaths() const
    {
        return m_paths;
    }
    QPixmap thumbReady(const QString &path) const;
    bool thumbFailed(const QString &path) const;
    // M55 observability for the bounded UI-side pixmap cache.
    int thumbReadyCount() const;
    qint64 thumbReadyBytes() const;
    // Public inspection follows the committed gallery result (post-filter),
    // while the private m_allEntries snapshot remains the full directory source
    // used to re-evaluate later query generations without rescanning.
    const QList<Entry> &entries() const
    {
        return m_displayEntries;
    }

    // M59 deterministic seam: evaluate the same immutable source/query
    // snapshot used by the background filter worker.  Keeping this wrapper
    // value-only lets the large-directory gate exercise the real Browse
    // evaluator without constructing one widget per row.
    static QList<Entry> evaluateQuerySnapshotForTest(
        const QList<Entry> &source, const QString &text, bool useFuzzy,
        const QRegularExpression &fuzzy, const mviewer::core::BrowseQuery &query,
        const mviewer::core::RatingStore::Snapshot &ratings,
        const mviewer::core::TagStore::Snapshot &tags, const QHash<QString, QString> &metaIndex,
        const QHash<QString, int> &metaIso, const QHash<QString, QString> &metaCamera,
        const QHash<QString, QString> &metaLens)
    {
        return evaluateFilterSnapshot(source, text, useFuzzy, fuzzy, query, ratings, tags,
                                      metaIndex, metaIso, metaCamera, metaLens);
    }

    // Resolve a path to the current filtered model row (scroll / repaint).
    // This row must never be used to index m_allEntries.
    int rowForPath(const QString &path) const
    {
        return m_rowByPath.value(path, -1);
    }
    // Read-only path lookup for delegates. The gallery model may be filtered,
    // so a model row is not necessarily the same as the row in m_allEntries.
    // M46: the displayed entries (m_displayEntries, rebuilt by buildModel) are
    // the authoritative paint-time data source — size/date are scan-cached and
    // the delegate NEVER queries the filesystem for them. The returned pointer
    // is valid until the panel rebuilds its model; delegates only use it
    // during one paint call.
    const Entry *entryForPath(const QString &path) const
    {
        const int row = m_displayEntryRow.value(path, -1);
        if (row >= 0 && row < m_displayEntries.size())
            return &m_displayEntries.at(row);
        const int srcRow = m_sourceRowByPath.value(path, -1);
        return srcRow >= 0 && srcRow < m_allEntries.size() ? &m_allEntries.at(srcRow) : nullptr;
    }
    // P0 #①: EXIF accessors for the Details view columns (camera / lens / ISO).
    // Backed by the metadata index built lazily in ensureMetaIndex().
    QString metaCameraForPath(const QString &path) const
    {
        return m_metaCamera.value(path);
    }
    QString metaLensForPath(const QString &path) const
    {
        return m_metaLens.value(path);
    }
    int metaIsoForPath(const QString &path) const
    {
        return m_metaIso.value(path, -1);
    }

    // P1: repaint the gallery to reflect a rating change made elsewhere.
    void invalidateRatings();

  signals:
    void itemClicked(const QString &path);
    void itemDoubleClicked(const QString &path);
    void viewModeChanged(ThumbnailPanel::ViewMode mode);
    void thumbSizeChanged(int size);
    void compareRequested(const QStringList &paths);
    // External files/folders dropped onto the gallery (forwarded to MainWindow).
    void filesDropped(const QStringList &paths);
    // Emitted after a delete/trash operation so the host can advance the viewer
    // off the deleted image. `deletedPaths` lists the removed files.
    void pathsRemoved(const QStringList &deletedPaths);
    // M56: path identity migration for an inferred rename.
    void pathsRenamed(const QStringList &oldPaths, const QStringList &newPaths);
    // M56: same-path content changes require source/cache refresh, not row
    // replacement or directory navigation.
    void pathsModified(const QStringList &paths);
    // P0 #①: live gallery stats for the status bar (count / sizes / selection).
    void statsChanged(int total, qint64 totalBytes, int selected, qint64 selectedBytes);
    // P0-2: the image under the cursor (gallery hover).
    void hovered(const QString &path);
    // M37: final visible order after the async scan and active sort/filters.
    void sequenceChanged(const QString &directory, const QStringList &paths);
    void directorySourceChanged(const QString &path);
    // M56: explicit watcher/F5 hint for the active directory. The host routes
    // it to DirectoryMonitor; this is not a navigation signal.
    void directoryContentsChanged(const QString &path);

  private slots:
    void onThumbReady(const QString &path);
    // M24: coalesced dataChanged flush (see onThumbReady).
    void flushThumbUpdates();
    void onSelectionChanged();

  private:
    void replaceDelegate(QStyledItemDelegate *delegate);
    void configureLargeIconMode();
    void configureSmallIconMode();
    void configureDetailsMode();
    void configureFilmstripMode();
    void configureCompactMode();
    void configureListMode();
    void configureThumbnailMode();
    void buildModel(const QList<Entry> &entries);
    bool takePendingFilterRestore(bool hasEntries, QStringList &selection, QString &current);
    void updateVisibleRange();
    void onCompareClicked();
    QString thumbCacheKey(const QString &path, int size) const;
    void enforceThumbPixmapBudgetLocked();

    static QFileInfoList sortedEntries(const QDir &dir, SortMode mode, bool ascending = true);
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    void mouseReleaseEvent(QMouseEvent *event) override;
    void mouseDoubleClickEvent(QMouseEvent *event) override;
    // Ctrl+wheel adjusts the thumbnail size (Explorer/FastStone parity).
    void wheelEvent(QWheelEvent *event) override;
    // External drag & drop of files/folders onto the gallery.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    class ThumbDelegate;
    class DetailsDelegate;
    class ListDelegate;

    QStringList m_paths;                   // actual file paths, aligned with model
    QHash<QString, int> m_rowByPath;       // path -> model row (scroll / repaint)
    QHash<QString, int> m_sourceRowByPath; // path -> m_allEntries row (metadata)
    QHash<QString, qint64> m_sizeByPath;   // path -> byte size (selection stats)
    QStringListModel *m_model = nullptr;
    QStyledItemDelegate *m_delegate = nullptr;

    // Thread-safe ready thumbnail pixmaps (filled from the pipeline result fn).
    mutable QMutex m_thumbMtx;
    struct ReadyPixmap
    {
        QPixmap pixmap;
        qint64 bytes = 0;
        uint64_t lastUse = 0;
    };
    mutable QHash<QString, ReadyPixmap> m_thumbReady; // key = path + size
    mutable qint64 m_thumbReadyBytes = 0;
    mutable uint64_t m_thumbReadyClock = 0;
    QSet<QString> m_thumbPending;
    QSet<QString> m_thumbFailed; // key = path + size; decode returned null

    QPushButton *m_compareBtn = nullptr;
    QString m_currentDir;
    CommandStack *m_cmdStack = nullptr;    // A-10: optional reversible file ops
    SelectionModel *m_selection = nullptr; // P0-2: hover target (not owned)
    // M24: bounded worker pool for the directory scan / dimension resolve
    // tasks (max 2). Replaces detached std::thread so rapid folder switching
    // can never grow thread count without bound, and destruction waits for
    // in-flight tasks (which abort early via m_alive).
    QThreadPool m_scanPool;
    // M24 (S3): one pending coalesced DecorationRole flush per event-loop turn.
    bool m_thumbDirty = false;
    QSet<QString> m_thumbDirtyPaths;
    // M24 (A#8): single-path selection requested while the directory model is
    // still being rebuilt asynchronously (e.g. rename→rescan). Applied by
    // buildModel once the path is present; cleared when applied.
    QString m_pendingSelect;
    SortMode m_sortMode = SortName;
    bool m_sortAscending = true; // A-2.2: sort direction
    QString m_typeFilter;        // A-2.3: comma-separated type filter
    ViewMode m_viewMode = Thumbnail;
    int m_thumbSize = kDefaultThumbSize;     // M15: dynamic thumb size
    int m_gridThumbSize = kDefaultThumbSize; // Last user-selected standard grid size.
    // A Ctrl/Shift selection gesture changes the selection without opening a
    // single image. Keep this set through the mouse release because Qt emits
    // clicked/currentChanged while the gesture is being processed.
    bool m_selectionGesture = false;
    // Stable path anchor for Shift ranges. Qt's IconMode selection anchor is
    // not consistent across the Windows styles used by the native view, so
    // keep the anchor at the panel boundary and apply the same range semantics
    // for real mouse events.
    QString m_selectionAnchorPath;
    QString m_filterText;
    bool m_filterRecursive = false;
    qint64 m_totalBytes = 0;
    bool m_pipelineWired = false;
    void wireThumbnailPipeline();
    void resetDirectoryState();

    // P1: filter state for metadata search + star-rating filter.
    QList<Entry> m_allEntries; // full listing; source for filtering
    // M46: the entries currently DISPLAYED (post-filter, post-recursive-hit).
    // The delegates paint exclusively from this list via entryForPath(), so
    // size/mtime never need a filesystem query at paint time. Rebuilt by
    // buildModel(); kept in sync with m_paths.
    QList<Entry> m_displayEntries;
    QHash<QString, int> m_displayEntryRow; // path -> row in m_displayEntries
    bool m_metaSearch = false;             // search embedded metadata, not just names
    // M25: async metadata indexing (shared MetadataIndexer) + recursive scan
    // state — both generation-scoped so directory switches cancel them.
    bool m_metaIndexing = false;
    // M26: the MetadataIndexer request owned by this panel's filter index — a
    // new filter/directory request supersedes ONLY this request, never the
    // MainWindow search re-index.
    uint64_t m_metaRequestId = 0;
    bool m_recursiveSearching = false;
    QList<Entry> m_recursiveHits; // recursive filename-search results (merged by applyFilter)
    QString m_recursiveHitsFor;   // the filter text the current hits belong to
    int m_ratingFilter = 0;       // show only images rated >= this (0 = all)
    int m_labelFilter = 0;        // show only images with this color label (0 = any)
    bool m_rejectFilter = false;  // show only rejected images
    bool m_pickFilter = false;    // show only picked (favorite) images
    bool m_recentFilter = false;  // show only recently-viewed images
    QString m_cameraFilter;       // P0 #①: camera make/model substring
    QString m_lensFilter;         // P0 #①: lens model substring
    int m_isoFilter = 0;          // P0 #①: exact ISO (0 = any)
    QString m_tagFilter;          // P0 #①: exact tag (empty = any)
    QHash<QString, QString> m_metaIndex;  // path -> lowercase searchable string
    QHash<QString, int> m_metaIso;        // path -> ISO (for exact ISO filter)
    QHash<QString, QString> m_metaCamera; // path -> "make model" (Details EXIF column)
    QHash<QString, QString> m_metaLens;   // path -> lens model (Details EXIF column)

    QTimer *m_filterDebounceTimer = nullptr;
    std::shared_ptr<std::atomic<bool>> m_filterCancel;
    TaskScheduler::TaskHandle m_filterTask;
    uint64_t m_filterGeneration = 0;
    // Small directories clear their stale projection immediately while the
    // debounced query is pending. Keep the path identity so the eventual
    // result can restore selection/current-image exactly.
    QStringList m_pendingFilterSelection;
    QString m_pendingFilterCurrent;
    uint64_t m_pendingFilterGeneration = 0;

    void applyFilter(); // schedule a latest-wins filtered model rebuild
    void scheduleFilter(bool debounce);
    void runFilterQuery();
    bool prepareFilterSource(const QString &text, QList<Entry> &source);
    bool matchesFilter(const Entry &entry, const QString &text, bool useFuzzy,
                       const QRegularExpression &fuzzy) const;
    static bool matchesFilterSnapshot(const Entry &entry, const QString &text, bool useFuzzy,
                                      const QRegularExpression &fuzzy,
                                      const mviewer::core::BrowseQuery &query,
                                      const mviewer::core::RatingStore::Snapshot &ratings,
                                      const mviewer::core::TagStore::Snapshot &tags,
                                      const QHash<QString, QString> &metaIndex,
                                      const QHash<QString, int> &metaIso,
                                      const QHash<QString, QString> &metaCamera,
                                      const QHash<QString, QString> &metaLens);
    static QList<Entry> evaluateFilterSnapshot(const QList<Entry> &source, const QString &text,
                                               bool useFuzzy, const QRegularExpression &fuzzy,
                                               const mviewer::core::BrowseQuery &query,
                                               const mviewer::core::RatingStore::Snapshot &ratings,
                                               const mviewer::core::TagStore::Snapshot &tags,
                                               const QHash<QString, QString> &metaIndex,
                                               const QHash<QString, int> &metaIso,
                                               const QHash<QString, QString> &metaCamera,
                                               const QHash<QString, QString> &metaLens);
    void ensureMetaIndex(); // lazily index metadata for m_allEntries
    void applyThumbSize(int size, bool rememberGridSize);
    void runBatchAnalyzeExportAsync(const QStringList &paths, const std::string &analyzerId,
                                    const QString &output);
    void startCommandFileOperation(std::unique_ptr<ICommand> command, const QStringList &paths,
                                   const QString &label);
    void startCopyFileOperation(const QStringList &paths, const QString &destinationDirectory);
    void applyDisplayedEntriesIncremental(const QList<Entry> &entries,
                                          const QStringList &previousSelection,
                                          const QString &previousCurrent, const QString &anchorPath,
                                          int anchorOffset);
    void preserveScrollAnchor(const QString &anchorPath, int anchorOffset);
    void applyDirectoryDeltaEntries(const mviewer::core::DirectoryDelta &delta, QList<Entry> &next,
                                    QStringList &removedPaths, QStringList &renamedFrom,
                                    QStringList &renamedTo);
    void sortDirectoryDeltaEntries(QList<Entry> &entries) const;
    void captureIncrementalDeltaState(QStringList &selection, QString &current, QString &anchorPath,
                                      int &anchorOffset, int &currentRow) const;

    // P0-1 (perf): resolve pixel dimensions off the UI thread. setDirectory no
    // longer reads image headers eagerly (that blocked folder switching on large
    // directories); dimensions are filled in the background only when the
    // Details view needs them. m_dirGen invalidates stale background results.
    void ensureDimensions();
    int m_dirGen = 0;
    bool m_dimsResolved = false;
    // M54: default name-ascending scans can publish rows while enumeration is
    // still running. A filter/sort requested mid-scan disables provisional UI.
    bool m_scanProgressive = false;
    bool m_scanComplete = true;
    bool m_scanRangeUpdatePending = false;
    bool m_incrementalApply = false;
    bool m_directoryUnavailable = false;
    bool m_liveDirectoryMonitoring = false;
    QStringList m_incrementalPrevSelection;
    QString m_incrementalPrevCurrent;
    QString m_incrementalAnchorPath;
    int m_incrementalAnchorOffset = 0;
    int m_incrementalPreviousCurrentRow = -1;
    // M46: directory-generation token shared with the scan/dimension/recursive
    // workers. setDirectory() stores the new generation here; every worker
    // loop re-checks it (alongside m_alive) so superseded walking, sorting and
    // dimension probing stop cooperatively instead of running to completion
    // only to be discarded on the UI thread.
    std::shared_ptr<std::atomic<uint64_t>> m_scanGenToken;
    // M46: app-global busy-cursor refcount, owned UI-side. setDirectory()
    // increments it before launching the scan; every scan completion or abort
    // decrements it (marshaled to the UI thread), and the destructor drains
    // any refs whose worker was dropped by m_scanPool.clear(). This makes
    // setOverrideCursor()/restoreOverrideCursor() provably balanced: no queued
    // job being cleared/cancelled can strand the whole app with a busy cursor.
    std::shared_ptr<std::atomic<int>> m_busyCursorRefs;

    // P0-4: column-title header row shown only in the Details view. Positioned in
    // the reserved viewport top margin so it lines up with the delegate columns.
    QWidget *m_detailsHeader = nullptr;
    void positionDetailsHeader();

    // Guards against the shared pipeline's worker thread calling back into a
    // destroyed panel after the destructor runs.
    std::shared_ptr<std::atomic<bool>> m_alive;
    TaskScheduler::TaskHandle m_batchTask;
    QProgressDialog *m_batchProgress = nullptr;
    TaskScheduler::TaskHandle m_fileOperationTask;
    QProgressDialog *m_fileProgress = nullptr;
    uint64_t m_fileOperationGeneration = 0;
    bool m_fileOperationBusy = false;
};

// Paints only the visible cells: a (cached/decoded) thumbnail + filename. No
// widget is created per image, so the gallery scales to very large directories.
class ThumbnailPanel::ThumbDelegate : public QStyledItemDelegate
{
  public:
    explicit ThumbDelegate(ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_panel(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

  private:
    int thumbSize() const; // reads m_panel->thumbSize()
    ThumbnailPanel *m_panel;
};

// Details / List mode delegate: renders each row as a horizontal strip with
// columns for thumbnail, filename, resolution, size, date, and format.
class ThumbnailPanel::DetailsDelegate : public QStyledItemDelegate
{
  public:
    explicit DetailsDelegate(ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_panel(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

  private:
    ThumbnailPanel *m_panel;
};

// P0: Windows-Explorer-style list — a small icon plus the file name, wrapping
// into columns. Used by ViewMode::List. Lighter than Details (no columns).
class ThumbnailPanel::ListDelegate : public QStyledItemDelegate
{
  public:
    explicit ListDelegate(ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_panel(panel)
    {
    }
    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option, const QModelIndex &index) const override;

  private:
    ThumbnailPanel *m_panel;
};
