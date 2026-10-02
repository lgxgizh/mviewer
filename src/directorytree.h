#pragma once

#include <functional>

#include <QLineEdit>
#include <QListView>
#include <QSortFilterProxyModel>
#include <QStringList>
#include <QTreeView>

class QFileSystemModel;
class QFileSystemWatcher;
class QLabel;
class QContextMenuEvent;
class QKeyEvent;
class QPainter;
class QStyleOptionViewItem;
class QTimer;

class DirectoryProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    explicit DirectoryProxyModel(QObject *parent = nullptr);
    void setFilterText(const QString &text);
    QString filterText() const
    {
        return m_filterText;
    }

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
    // Check whether any descendant of sourceParent matches the current filter text.
    bool hasAcceptedDescendant(const QModelIndex &sourceParent, int depth = 0) const;

  private:
    QString m_filterText;
};

// Production-grade directory tree for image browsing.
//
// Features (A-1 review action items):
//   * Auto-sync: navigateTo() expands all ancestors and highlights the active dir
//   * Tree refresh: QFileSystemWatcher refreshes expanded directory nodes
//     without owning the active Browse-directory lifecycle.
//   * Large-dir async: fetchMore is driven by the model; loading indicator shown
//   * Current-dir highlight: bold + accent background on the active node
class DirectoryTree : public QTreeView
{
    Q_OBJECT

  public:
    explicit DirectoryTree(QWidget *parent = nullptr);
    ~DirectoryTree() override;

    // Navigate the tree to the given path: expand parents, scroll to, and
    // select the item. If `emitSignal` is false, directoryChanged is suppressed
    // so callers can drive the tree programmatically without loops.
    void navigateTo(const QString &path, bool emitSignal = false);

    // The directory currently selected in the tree (empty if none).
    QString currentPath() const;

    // Access the filter line-edit so callers can place it in a layout.
    /// Hidden; retained for clear-on-navigate and tests. Prefer pathEdit.
    QLineEdit *filterEdit() const
    {
        return m_filterEdit;
    }

  public slots:
    // F5 refresh. Re-reads the currently selected folder from disk and sends a
    // contents hint. It does not masquerade as a directory navigation.
    void refresh();
    // Gallery listed its first batch, or the scan settled empty. Highlight work
    // stays queued until this so QFileSystemModel::index cannot sit in front of
    // the first paint. Tree-only callers fall through on a timer.
    void nudgeDeferredHighlight();
    // Test seam. Invoked on the navigation worker after isDir succeeds and
    // before any GUI index(). Empty in production. A blocking hook must not
    // freeze the gallery.
    static void setNavigationIndexProbeHook(const std::function<void()> &hook);

  signals:
    // A committed A -> B navigation. MainWindow owns navigation side effects.
    void directoryChanged(const QString &path);
    // A watcher/F5 hint that the current directory's contents may have changed.
    // Consumers reconcile a snapshot; no navigation lifecycle is allowed.
    void directoryContentsChanged(const QString &path);

  protected:
    // Enter/Return opens the selected directory (same as double-click) so the
    // tree is fully keyboard-navigable.
    void keyPressEvent(QKeyEvent *event) override;
    // Right-click context menu: "在资源管理器中显示" + "复制路径".
    void contextMenuEvent(QContextMenuEvent *event) override;
    // Paint the current-directory highlight (bold + accent background).
    void drawRow(QPainter *painter, const QStyleOptionViewItem &option,
                 const QModelIndex &index) const override;

  private slots:
    void onDirectoryChanged(const QString &path);
    void onDirectoryLoaded(const QString &path);
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onExpanded(const QModelIndex &index);

  private:
    static bool equivalentPath(const QString &left, const QString &right);
    void watchPath(const QString &path);
    void setLoading(bool on);
    void applyCurrentHighlight(const QModelIndex &proxyIdx);
    QModelIndex sourceIndexForPath(const QString &path) const;
    void expandAncestors(const QModelIndex &sourceIdx);
    void tryNavigateToPending(quint64 requestId);
    void finishPendingNavigation(quint64 requestId);
    void scheduleDeferredNavigation(quint64 requestId);
    void armHighlightFallback(quint64 requestId);
    void scheduleNavigationRetry(quint64 requestId);
    void resolvePendingNavigation(quint64 requestId);
    void acceptNavigationStat(quint64 requestId, bool isDir);
    void cancelPendingNavigation();
    // A-1.5: progressive fetchMore for large directories (yields to event loop).
    void scheduleFetchMore(const QModelIndex &sourceIdx);

    QFileSystemModel *m_model = nullptr;
    DirectoryProxyModel *m_proxy = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QString m_currentPath; // last navigated / selected path (for highlight)
    // Bounded tree-only watcher set. The active Browse directory is retained
    // when another tree node is expanded; gallery ownership lives in
    // DirectoryMonitor, not in this view.
    QStringList m_watchedPaths;
    bool m_loading = false;
    QLabel *m_loadingLabel = nullptr;
    QString m_pendingFetchPath; // path currently being progressively fetched
    QLineEdit *m_filterEdit = nullptr;
    QTimer *m_navigationRetryTimer = nullptr;
    QString m_pendingNavigationPath;
    bool m_pendingNavigationEmitSignal = false;
    quint64 m_navigationRequestId = 0;
    int m_navigationRetryCount = 0;
    // True only after a worker QFileInfo::isDir() succeeded. The GUI indexes one
    // path prefix per event-loop turn after that, and only once the gallery has
    // had a chance to paint (or the tree-only fallback fires).
    bool m_navigationStatReady = false;
    // emitSignal navigations own a gallery scan. Hold the expensive model index
    // until that scan nudges, so the first paint is not stuck behind it.
    bool m_holdHighlightForGallery = false;
    bool m_highlightStarted = false;
    bool m_highlightNudgePending = false;
    bool m_deferredNavigationQueued = false;
    int m_navigationSegment = 0;
    QStringList m_navigationPrefixes;
};
