#pragma once

#include <atomic>
#include <memory>

#include <QDir>
#include <QFileInfo>
#include <QHash>
#include <QListView>
#include <QMutex>
#include <QPixmap>
#include <QSet>
#include <QSize>
#include <QStringList>
#include <QShowEvent>
#include <QStyledItemDelegate>

class QPushButton;
class QContextMenuEvent;
class QResizeEvent;
class QStringListModel;

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
    static constexpr int kThumbSize = 140;

    enum SortMode
    {
        SortName,
        SortDate,
        SortSize,
        SortResolution
    };

    explicit ThumbnailPanel(QWidget *parent = nullptr);
    ~ThumbnailPanel() override;

    void setDirectory(const QString &path);
    void setSortMode(SortMode mode);

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

    // M13.4: run a chosen analyzer over every selected image and export the
    // structured per-image metrics to CSV/JSON. Drives core AnalyzerRegistry.
    void batchAnalyzeExport();

    // P0 #①: read access for the delegate (paths + ready pixmaps).
    const QStringList &pathList() const { return m_paths; }
    QPixmap thumbReady(const QString &path) const;

    // P1: repaint the gallery to reflect a rating change made elsewhere.
    void invalidateRatings();

  signals:
    void itemClicked(const QString &path);
    void itemDoubleClicked(const QString &path);
    void compareRequested(const QStringList &paths);
    // P0 #①: live gallery stats for the status bar (count / sizes / selection).
    void statsChanged(int total, qint64 totalBytes, int selected, qint64 selectedBytes);

  private slots:
    void onThumbReady(const QString &path);
    void onSelectionChanged();

  private:
    struct Entry
    {
        QString path;
        QString name;
        qint64 size = 0;
    };

    void buildModel(const QList<Entry> &entries);
    void updateVisibleRange();
    void onCompareClicked();

    static QFileInfoList sortedEntries(const QDir &dir, SortMode mode);
    void contextMenuEvent(QContextMenuEvent *event) override;
    void resizeEvent(QResizeEvent *event) override;
    void showEvent(QShowEvent *event) override;

    class ThumbDelegate;

    QStringList m_paths;                       // actual file paths, aligned with model
    QHash<QString, int> m_rowByPath;           // path -> model row (scroll / repaint)
    QHash<QString, qint64> m_sizeByPath;       // path -> byte size (selection stats)
    QStringListModel *m_model = nullptr;
    ThumbDelegate *m_delegate = nullptr;

    // Thread-safe ready thumbnail pixmaps (filled from the pipeline result fn).
    mutable QMutex m_thumbMtx;
    QHash<QString, QPixmap> m_thumbReady;
    QSet<QString> m_thumbPending;

    QPushButton *m_compareBtn = nullptr;
    QString m_currentDir;
    SortMode m_sortMode = SortName;
    QString m_filterText;
    bool m_filterRecursive = false;
    qint64 m_totalBytes = 0;
    bool m_pipelineWired = false;

    // P1: filter state for metadata search + star-rating filter.
    QList<Entry> m_allEntries;                 // full listing; source for filtering
    bool m_metaSearch = false;                 // search embedded metadata, not just names
    int m_ratingFilter = 0;                    // show only images rated >= this (0 = all)
    int m_labelFilter = 0;                     // show only images with this color label (0 = any)
    bool m_rejectFilter = false;               // show only rejected images
    bool m_pickFilter = false;                 // show only picked (favorite) images
    bool m_recentFilter = false;               // show only recently-viewed images
    QHash<QString, QString> m_metaIndex;       // path -> lowercase searchable string

    void applyFilter();                        // (re)build the filtered model
    void ensureMetaIndex();                    // lazily index metadata for m_allEntries

    // Guards against the shared pipeline's worker thread calling back into a
    // destroyed panel after the destructor runs.
    std::shared_ptr<std::atomic<bool>> m_alive;
};

// Paints only the visible cells: a (cached/decoded) thumbnail + filename. No
// widget is created per image, so the gallery scales to very large directories.
class ThumbnailPanel::ThumbDelegate : public QStyledItemDelegate
{
  public:
    explicit ThumbDelegate(int thumbSize, ThumbnailPanel *panel, QObject *parent = nullptr)
        : QStyledItemDelegate(parent), m_thumbSize(thumbSize), m_panel(panel)
    {
    }

    void paint(QPainter *painter, const QStyleOptionViewItem &option,
               const QModelIndex &index) const override;
    QSize sizeHint(const QStyleOptionViewItem &option,
                   const QModelIndex &index) const override;

  private:
    int m_thumbSize;
    ThumbnailPanel *m_panel;
};
