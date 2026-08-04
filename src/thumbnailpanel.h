#pragma once

#include <atomic>
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

#include "core/TagStore.h"

class QPushButton;
class QContextMenuEvent;
class QResizeEvent;
class QStringListModel;
class CommandStack;
class SelectionModel;

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

    // Scroll the grid so the item for `path` is visible and select it. Used by
    // browse-position restore (reopen last image after launch).
    void scrollToPath(const QString &path);
    // Current vertical scroll offset of the thumbnail grid (for persistence).
    int scrollOffset() const;

    QStringList selectedPaths() const;

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
    const QList<Entry> &entries() const
    {
        return m_allEntries;
    }

    // Resolve a path to the current filtered model row (scroll / repaint).
    // This row must never be used to index m_allEntries.
    int rowForPath(const QString &path) const
    {
        return m_rowByPath.value(path, -1);
    }
    // Read-only path lookup for delegates. The gallery model may be filtered,
    // so a model row is not necessarily the same as the row in m_allEntries.
    // The returned pointer is valid until the panel rebuilds its directory
    // model; delegates only use it during one paint call.
    const Entry *entryForPath(const QString &path) const
    {
        const int row = m_sourceRowByPath.value(path, -1);
        return row >= 0 && row < m_allEntries.size() ? &m_allEntries.at(row) : nullptr;
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
    void compareRequested(const QStringList &paths);
    // External files/folders dropped onto the gallery (forwarded to MainWindow).
    void filesDropped(const QStringList &paths);
    // Emitted after a delete/trash operation so the host can advance the viewer
    // off the deleted image. `deletedPaths` lists the removed files.
    void pathsRemoved(const QStringList &deletedPaths);
    // P0 #①: live gallery stats for the status bar (count / sizes / selection).
    void statsChanged(int total, qint64 totalBytes, int selected, qint64 selectedBytes);
    // P0-2: the image under the cursor (gallery hover).
    void hovered(const QString &path);

  private slots:
    void onThumbReady(const QString &path);
    void onSelectionChanged();

  private:
    void buildModel(const QList<Entry> &entries);
    void updateVisibleRange();
    void onCompareClicked();

    static QFileInfoList sortedEntries(const QDir &dir, SortMode mode, bool ascending = true);
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;
    void mousePressEvent(QMouseEvent *event) override;
    // Ctrl+wheel adjusts the thumbnail size (Explorer/FastStone parity).
    void wheelEvent(QWheelEvent *event) override;
    // External drag & drop of files/folders onto the gallery.
    void dragEnterEvent(QDragEnterEvent *event) override;
    void dragMoveEvent(QDragMoveEvent *event) override;
    void dropEvent(QDropEvent *event) override;

    class ThumbDelegate;
    class DetailsDelegate;
    class ListDelegate;

    QStringList m_paths;                 // actual file paths, aligned with model
    QHash<QString, int> m_rowByPath;     // path -> model row (scroll / repaint)
    QHash<QString, int> m_sourceRowByPath; // path -> m_allEntries row (metadata)
    QHash<QString, qint64> m_sizeByPath; // path -> byte size (selection stats)
    QStringListModel *m_model = nullptr;
    QStyledItemDelegate *m_delegate = nullptr;

    // Thread-safe ready thumbnail pixmaps (filled from the pipeline result fn).
    mutable QMutex m_thumbMtx;
    QHash<QString, QPixmap> m_thumbReady;
    QSet<QString> m_thumbPending;
    QSet<QString> m_thumbFailed; // decode returned null → show error placeholder

    QPushButton *m_compareBtn = nullptr;
    QString m_currentDir;
    CommandStack *m_cmdStack = nullptr;    // A-10: optional reversible file ops
    SelectionModel *m_selection = nullptr; // P0-2: hover target (not owned)
    SortMode m_sortMode = SortName;
    bool m_sortAscending = true; // A-2.2: sort direction
    QString m_typeFilter;        // A-2.3: comma-separated type filter
    ViewMode m_viewMode = Thumbnail;
    int m_thumbSize = kDefaultThumbSize; // M15: dynamic thumb size
    QString m_filterText;
    bool m_filterRecursive = false;
    qint64 m_totalBytes = 0;
    bool m_pipelineWired = false;

    // P1: filter state for metadata search + star-rating filter.
    QList<Entry> m_allEntries;            // full listing; source for filtering
    bool m_metaSearch = false;            // search embedded metadata, not just names
    int m_ratingFilter = 0;               // show only images rated >= this (0 = all)
    int m_labelFilter = 0;                // show only images with this color label (0 = any)
    bool m_rejectFilter = false;          // show only rejected images
    bool m_pickFilter = false;            // show only picked (favorite) images
    bool m_recentFilter = false;          // show only recently-viewed images
    QString m_cameraFilter;               // P0 #①: camera make/model substring
    QString m_lensFilter;                 // P0 #①: lens model substring
    int m_isoFilter = 0;                  // P0 #①: exact ISO (0 = any)
    QString m_tagFilter;                  // P0 #①: exact tag (empty = any)
    QHash<QString, QString> m_metaIndex;  // path -> lowercase searchable string
    QHash<QString, int> m_metaIso;        // path -> ISO (for exact ISO filter)
    QHash<QString, QString> m_metaCamera; // path -> "make model" (Details EXIF column)
    QHash<QString, QString> m_metaLens;   // path -> lens model (Details EXIF column)

    void applyFilter();     // (re)build the filtered model
    void ensureMetaIndex(); // lazily index metadata for m_allEntries

    // P0-1 (perf): resolve pixel dimensions off the UI thread. setDirectory no
    // longer reads image headers eagerly (that blocked folder switching on large
    // directories); dimensions are filled in the background only when the
    // Details view needs them. m_dirGen invalidates stale background results.
    void ensureDimensions();
    int m_dirGen = 0;
    bool m_dimsResolved = false;

    // P0-4: column-title header row shown only in the Details view. Positioned in
    // the reserved viewport top margin so it lines up with the delegate columns.
    QWidget *m_detailsHeader = nullptr;
    void positionDetailsHeader();

    // Guards against the shared pipeline's worker thread calling back into a
    // destroyed panel after the destructor runs.
    std::shared_ptr<std::atomic<bool>> m_alive;
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
