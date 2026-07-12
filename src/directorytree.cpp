#include "directorytree.h"

#include <QDir>
#include <QFileSystemModel>

namespace
{
const QStringList kImageExtensions = {".jpg", ".jpeg", ".bmp", ".png"};

bool containsImages(const QString &dirPath)
{
    QDir dir(dirPath);
    if (!dir.exists())
        return false;

    const QFileInfoList entries = dir.entryInfoList(
        QDir::Files | QDir::NoDotAndDotDot);
    for (const QFileInfo &info : entries) {
        const QString suffix = info.suffix().toLower();
        if (kImageExtensions.contains("." + suffix))
            return true;
    }
    return false;
}
}

DirectoryProxyModel::DirectoryProxyModel(QObject *parent)
    : QSortFilterProxyModel(parent)
{
}

bool DirectoryProxyModel::filterAcceptsRow(int sourceRow,
                                          const QModelIndex &sourceParent) const
{
    QFileSystemModel *fsModel = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fsModel)
        return true;

    const QModelIndex index = fsModel->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;

    if (!fsModel->isDir(index))
        return false;

    const QString path = fsModel->filePath(index);

    if (fsModel->fileName(index).isEmpty())
        return true;

    if (fsModel->fileInfo(index).isRoot())
        return true;

    return containsImages(path);
}

DirectoryTree::DirectoryTree(QWidget *parent)
    : QTreeView(parent)
{
    m_model = new QFileSystemModel(this);
    m_model->setRootPath(QDir::homePath());
    m_model->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);

    m_proxy = new DirectoryProxyModel(this);
    m_proxy->setSourceModel(m_model);

    setModel(m_proxy);
    setRootIndex(m_proxy->mapFromSource(m_model->index(QDir::homePath())));
    setHeaderHidden(true);
    setColumnHidden(1, true);
    setColumnHidden(2, true);
    setColumnHidden(3, true);
    setAnimated(true);
    setIndentation(15);

    connect(this, &QTreeView::clicked, this, [this](const QModelIndex &index) {
        const QModelIndex source = m_proxy->mapToSource(index);
        const QString path = m_model->filePath(source);
        if (m_model->isDir(source))
            emit directoryChanged(path);
    });
}

DirectoryTree::~DirectoryTree() = default;
