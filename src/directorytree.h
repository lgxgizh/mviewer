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

  signals:
    void directoryChanged(const QString &path);

  private:
    QFileSystemModel *m_model = nullptr;
    DirectoryProxyModel *m_proxy = nullptr;
};
