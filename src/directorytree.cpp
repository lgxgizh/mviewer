#include "directorytree.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QStyleOptionViewItem>
#include <QTimer>

namespace
{
const QStringList kImageExtensions = {".jpg",  ".jpeg", ".bmp", ".png", ".tif", ".tiff",
                                      ".webp", ".gif",  ".ico", ".pcx", ".tga", ".ppm"};

// Threshold above which we show a brief loading indicator while expanding.
constexpr int kLargeDirThreshold = 500;
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

    // Show all directories for an Explorer-like navigation experience.
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

    // A-1.3: current-directory highlight — use a custom style so the active
    // node is visually distinct from the regular selection.
    setStyleSheet(
        "DirectoryTree::item:selected { background: #2a82da; color: white; }"
        "DirectoryTree::item { padding: 2px 0; }");

    // A-1.4: QFileSystemWatcher for auto-refresh when the file system changes.
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &DirectoryTree::onDirectoryChanged);

    // A-1.5: when rows are inserted (model fetched children), clear loading.
    connect(m_model, &QFileSystemModel::rowsInserted, this, &DirectoryTree::onRowsInserted);

    // A-1.1: auto-expand parent nodes when a directory is expanded in the tree.
    connect(this, &QTreeView::expanded, this, &DirectoryTree::onExpanded);

    connect(this, &QTreeView::clicked, this,
            [this](const QModelIndex &index)
            {
                const QModelIndex source = m_proxy->mapToSource(index);
                const QString path = m_model->filePath(source);
                if (m_model->isDir(source))
                {
                    m_currentPath = path;
                    watchPath(path);
                    emit directoryChanged(path);
                }
            });

    // A-1.6: loading indicator label (shown briefly while expanding large dirs).
    m_loadingLabel = new QLabel("  加载中...", this);
    m_loadingLabel->setStyleSheet("color: #888; font-style: italic; padding: 4px;");
    m_loadingLabel->hide();
}

DirectoryTree::~DirectoryTree() = default;

QString DirectoryTree::currentPath() const
{
    const QModelIndex idx = currentIndex();
    if (!idx.isValid())
        return {};
    const QModelIndex source = m_proxy->mapToSource(idx);
    if (!m_model->isDir(source))
        return {};
    return m_model->filePath(source);
}

void DirectoryTree::refresh()
{
    const QString path = currentPath();
    if (path.isEmpty())
        return;
    // Nudge QFileSystemModel to re-scan this node's children so freshly created
    // or removed sub-folders are reflected immediately.
    const QModelIndex source = m_model->index(path);
    if (source.isValid())
        m_model->fetchMore(source);
    emit directoryChanged(path);
}

void DirectoryTree::keyPressEvent(QKeyEvent *event)
{
    // Enter/Return opens the selected directory (same as double-click).
    if (event->key() == Qt::Key_Return || event->key() == Qt::Key_Enter)
    {
        const QString path = currentPath();
        if (!path.isEmpty())
        {
            m_currentPath = path;
            watchPath(path);
            emit directoryChanged(path);
            event->accept();
            return;
        }
    }
    QTreeView::keyPressEvent(event);
}

void DirectoryTree::contextMenuEvent(QContextMenuEvent *event)
{
    const QModelIndex idx = indexAt(event->pos());
    if (!idx.isValid())
    {
        QTreeView::contextMenuEvent(event);
        return;
    }
    const QString path = m_model->filePath(m_proxy->mapToSource(idx));
    if (path.isEmpty())
    {
        QTreeView::contextMenuEvent(event);
        return;
    }

    QMenu menu(this);
    QAction *aOpen = menu.addAction("打开");
    QAction *aReveal = menu.addAction("在资源管理器中显示");
    QAction *aCopyPath = menu.addAction("复制路径");
    QAction *chosen = menu.exec(event->globalPos());
    if (!chosen)
        return;
    if (chosen == aOpen)
    {
        m_currentPath = path;
        watchPath(path);
        emit directoryChanged(path);
    }
    else if (chosen == aReveal)
    {
        QProcess::startDetached(
            "explorer.exe", {QStringLiteral("/select,\"%1\"").arg(QDir::toNativeSeparators(path))});
    }
    else if (chosen == aCopyPath)
        QApplication::clipboard()->setText(QDir::toNativeSeparators(path));
}

void DirectoryTree::navigateTo(const QString &path, bool emitSignal)
{
    if (path.isEmpty() || !QDir(path).exists())
        return;

    // Normalize to forward slashes so path parsing works on every platform.
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path));

    // Walk up the directory hierarchy to find the deepest existing source index.
    QString current = normalized;
    QModelIndex sourceIdx;
    while (!current.isEmpty())
    {
        sourceIdx = m_model->index(current);
        if (sourceIdx.isValid())
            break;
        int slash = current.lastIndexOf('/');
        if (slash <= 0)
            break;
        current = current.left(slash);
    }

    if (!sourceIdx.isValid())
        return;

    // A-1.2: expand all ancestors so the target node is visible.
    expandAncestors(sourceIdx);

    // A-1.5: for large directories, show loading indicator briefly.
    const int rowCount = m_model->rowCount(sourceIdx);
    if (rowCount >= kLargeDirThreshold || (rowCount == 0 && m_model->canFetchMore(sourceIdx)))
    {
        setLoading(true);
        m_model->fetchMore(sourceIdx);
    }

    const QModelIndex proxyIdx = m_proxy->mapFromSource(sourceIdx);
    if (!proxyIdx.isValid())
    {
        setLoading(false);
        return;
    }

    // A-1.1: auto-sync — update the current path and highlight.
    m_currentPath = normalized;
    watchPath(normalized);

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

    // A-1.3: apply visual highlight to the current directory.
    applyCurrentHighlight(proxyIdx);
    setLoading(false);
}

void DirectoryTree::onDirectoryChanged(const QString &path)
{
    // A-1.4: file system changed — refresh the affected node.
    const QModelIndex source = m_model->index(path);
    if (source.isValid())
    {
        m_model->fetchMore(source);
        // If the changed directory is the current one, re-emit so the gallery
        // reloads.
        if (path == m_currentPath)
            emit directoryChanged(path);
    }
}

void DirectoryTree::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    // A-1.5: when children appear after fetchMore, clear loading state.
    if (m_loading)
    {
        const QString parentPath = m_model->filePath(parent);
        if (parentPath == m_currentPath || parentPath == m_watchedPath)
            setLoading(false);
    }
}

void DirectoryTree::onExpanded(const QModelIndex &index)
{
    // A-1.1: when a node is expanded, ensure its children are fetched and
    // watch the directory for file system changes.
    const QModelIndex source = m_proxy->mapToSource(index);
    if (!source.isValid())
        return;
    const QString path = m_model->filePath(source);
    if (path.isEmpty())
        return;

    m_model->fetchMore(source);
    watchPath(path);
}

void DirectoryTree::watchPath(const QString &path)
{
    // A-1.4: add the path to the file system watcher so we get notified when
    // Explorer creates/deletes subdirectories.
    if (path.isEmpty() || path == m_watchedPath)
        return;
    if (!m_watchedPath.isEmpty())
        m_watcher->removePath(m_watchedPath);
    m_watchedPath = path;
    if (QDir(path).exists())
        m_watcher->addPath(path);
}

void DirectoryTree::setLoading(bool on)
{
    // A-1.6: show/hide the loading indicator.
    if (on && !m_loadingLabel->isVisible())
    {
        m_loadingLabel->move(8, 2);
        m_loadingLabel->show();
        m_loadingLabel->raise();
    }
    else if (!on)
    {
        m_loadingLabel->hide();
    }
    m_loading = on;
}

void DirectoryTree::applyCurrentHighlight(const QModelIndex &proxyIdx)
{
    // A-1.3: trigger a repaint so drawRow() can paint the highlight.
    Q_UNUSED(proxyIdx);
    viewport()->update();
}

QModelIndex DirectoryTree::sourceIndexForPath(const QString &path) const
{
    if (path.isEmpty())
        return {};
    return m_model->index(path);
}

void DirectoryTree::expandAncestors(const QModelIndex &sourceIdx)
{
    // A-1.2: expand every ancestor so the target node is visible in the tree.
    QModelIndex anc = sourceIdx.parent();
    while (anc.isValid())
    {
        m_model->fetchMore(anc);
        const QModelIndex proxyAnc = m_proxy->mapFromSource(anc);
        if (proxyAnc.isValid())
            expand(proxyAnc);
        anc = anc.parent();
    }
    // Also expand the target itself.
    m_model->fetchMore(sourceIdx);
    const QModelIndex proxyTarget = m_proxy->mapFromSource(sourceIdx);
    if (proxyTarget.isValid())
        expand(proxyTarget);
}

void DirectoryTree::drawRow(QPainter *painter, const QStyleOptionViewItem &option,
                            const QModelIndex &index) const
{
    // A-1.3: draw a subtle accent background + bold font on the current directory.
    const QModelIndex source = m_proxy->mapToSource(index);
    if (source.isValid())
    {
        const QString path = m_model->filePath(source);
        if (path == m_currentPath && !m_currentPath.isEmpty())
        {
            // Draw accent background.
            painter->save();
            QColor accent(42, 130, 218, 40); // semi-transparent blue
            painter->fillRect(option.rect, accent);
            painter->restore();

            // Draw with bold font.
            QStyleOptionViewItem boldOpt = option;
            QFont boldFont = boldOpt.font;
            boldFont.setBold(true);
            boldOpt.font = boldFont;
            QTreeView::drawRow(painter, boldOpt, index);
            return;
        }
    }
    QTreeView::drawRow(painter, option, index);
}
