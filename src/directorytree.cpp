#include "directorytree.h"

#include <QDir>
#include <QFileSystemModel>

namespace
{
const QStringList kImageExtensions = {
    ".jpg", ".jpeg", ".bmp", ".png", ".tif", ".tiff",
    ".webp", ".gif", ".ico", ".pcx", ".tga", ".ppm"};
} // namespace

DirectoryProxyModel::DirectoryProxyModel(QObject *parent) : QSortFilterProxyModel(parent)
{
}

bool DirectoryProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QFileSystemModel *fsModel = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fsModel)
        return true;

    const QModelIndex index = fsModel->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;

    // P0-1: show all directories for an Explorer-like navigation experience.
    // Hidden files remain filtered by the model filter flags.
    if (!fsModel->isDir(index))
        return false;

    return true;
}

DirectoryTree::DirectoryTree(QWidget *parent) : QTreeView(parent)
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

    connect(this, &QTreeView::clicked, this,
            [this](const QModelIndex &index)
            {
                const QModelIndex source = m_proxy->mapToSource(index);
                const QString path = m_model->filePath(source);
                if (m_model->isDir(source))
                    emit directoryChanged(path);
            });
}

DirectoryTree::~DirectoryTree() = default;

void DirectoryTree::navigateTo(const QString &path, bool emitSignal)
{
    if (path.isEmpty() || !QDir(path).exists())
        return;

    // Normalize to forward slashes so path parsing works on every platform
    // (QFileDialog returns backslashes on Windows; QFileSystemModel uses /).
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path));

    // Walk up the directory hierarchy to find the deepest existing source index.
    // QFileSystemModel lazily populates children, so we may need to trigger
    // fetchMore and then look again.
    QString current = normalized;
    QModelIndex sourceIdx;
    while (!current.isEmpty())
    {
        sourceIdx = m_model->index(current);
        if (sourceIdx.isValid())
            break;
        // Try the parent.
        int slash = current.lastIndexOf('/');
        if (slash <= 0)
            break;
        current = current.left(slash);
    }

    if (!sourceIdx.isValid())
        return;

    // Ensure children are fetched for each ancestor so the proxy picks up the
    // expanded tree.
    QStringList parts;
    {
        QString p = normalized;
        QString root = QDir::fromNativeSeparators(
            m_model->filePath(m_model->index(QDir::homePath())));
        while (p.size() > root.size() && p != root)
        {
            parts.prepend(p);
            int slash = p.lastIndexOf('/');
            if (slash <= 0)
                break;
            p = p.left(slash);
        }
    }

    QModelIndex currentSrc = m_model->index(parts.isEmpty() ? normalized : parts.last());
    if (!currentSrc.isValid())
        currentSrc = sourceIdx;

    // Expand every ancestor so the proxy rows are visible.
    QModelIndex anc = currentSrc.parent();
    while (anc.isValid())
    {
        m_model->fetchMore(anc);
        expand(m_proxy->mapFromSource(anc));
        anc = anc.parent();
    }

    m_model->fetchMore(currentSrc);
    const QModelIndex proxyIdx = m_proxy->mapFromSource(currentSrc);

    if (!proxyIdx.isValid())
        return;

    if (emitSignal)
    {
        // This will trigger the directoryChanged signal just like a user click.
        QTreeView::clicked(proxyIdx);
    }
    else
    {
        // Programmatic navigation: block signals to avoid re-triggering the
        // directoryChanged → setDirectory loop.
        selectionModel()->blockSignals(true);
        setCurrentIndex(proxyIdx);
        scrollTo(proxyIdx, PositionAtCenter);
        expand(proxyIdx);
        selectionModel()->blockSignals(false);
    }
}
