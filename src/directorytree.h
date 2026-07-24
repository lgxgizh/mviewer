#pragma once

#include <QListView>
#include <QSortFilterProxyModel>
#include <QTreeView>

class QFileSystemModel;
class QFileSystemWatcher;
class QLabel;
class QContextMenuEvent;
class QKeyEvent;
class QPainter;
class QStyleOptionViewItem;

class DirectoryProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    explicit DirectoryProxyModel(QObject *parent = nullptr);

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

// Production-grade directory tree for image browsing.
//
// Features (A-1 review action items):
//   * Auto-sync: navigateTo() expands all ancestors and highlights the active dir
//   * Auto-refresh: QFileSystemWatcher reloads when Explorer creates/deletes folders
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

  public slots:
    // F5 refresh. Re-reads the currently selected folder from disk (so
    // newly created / deleted sub-folders show up) and re-emits directoryChanged
    // so the gallery reloads too.
    void refresh();

  signals:
    void directoryChanged(const QString &path);

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
    void onRowsInserted(const QModelIndex &parent, int first, int last);
    void onExpanded(const QModelIndex &index);

  private:
    void watchPath(const QString &path);
    void setLoading(bool on);
    void applyCurrentHighlight(const QModelIndex &proxyIdx);
    QModelIndex sourceIndexForPath(const QString &path) const;
    void expandAncestors(const QModelIndex &sourceIdx);

    QFileSystemModel *m_model = nullptr;
    DirectoryProxyModel *m_proxy = nullptr;
    QFileSystemWatcher *m_watcher = nullptr;
    QString m_currentPath; // last navigated / selected path (for highlight)
    QString m_watchedPath; // currently watched directory
    bool m_loading = false;
    QLabel *m_loadingLabel = nullptr;
};
