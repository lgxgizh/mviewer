#pragma once

#include <QLineEdit>
#include <QListView>
#include <QStringList>
#include <QSortFilterProxyModel>
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
    bool hasAcceptedDescendant(const QModelIndex &sourceParent) const;

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
    QLineEdit *filterEdit() const
    {
        return m_filterEdit;
    }

  public slots:
    // F5 refresh. Re-reads the currently selected folder from disk and sends a
    // contents hint. It does not masquerade as a directory navigation.
    void refresh();

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
    void watchPath(const QString &path);
    void setLoading(bool on);
    void applyCurrentHighlight(const QModelIndex &proxyIdx);
    QModelIndex sourceIndexForPath(const QString &path) const;
    void expandAncestors(const QModelIndex &sourceIdx);
    void tryNavigateToPending(quint64 requestId);
    void scheduleNavigationRetry(quint64 requestId);
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
};
