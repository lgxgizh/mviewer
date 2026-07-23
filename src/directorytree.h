#pragma once

#include <QListView>
#include <QSortFilterProxyModel>
#include <QTreeView>

class QFileSystemModel;

class DirectoryProxyModel : public QSortFilterProxyModel
{
    Q_OBJECT

  public:
    explicit DirectoryProxyModel(QObject *parent = nullptr);

  protected:
    bool filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const override;
};

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
    // P0-1: F5 refresh. Re-reads the currently selected folder from disk (so
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

  private:
    QFileSystemModel *m_model = nullptr;
    DirectoryProxyModel *m_proxy = nullptr;
};
