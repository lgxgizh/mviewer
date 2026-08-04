#include "directorytree.h"

#include <QApplication>
#include <QClipboard>
#include <QContextMenuEvent>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QFileSystemWatcher>
#include <QLabel>
#include <QMenu>
#include <QPainter>
#include <QProcess>
#include <QStyleOptionViewItem>
#include <QSignalBlocker>
#include <QTimer>

namespace
{
const QStringList kImageExtensions = {".jpg",  ".jpeg", ".bmp", ".png", ".tif", ".tiff",
                                      ".webp", ".gif",  ".ico", ".pcx", ".tga", ".ppm"};

// Threshold above which we show a brief loading indicator while expanding.
constexpr int kLargeDirThreshold = 500;
constexpr int kMaxNavigationRetries = 8;
constexpr int kInitialNavigationRetryDelayMs = 50;
constexpr int kMaxNavigationRetryDelayMs = 1000;

bool equivalentPath(const QString &left, const QString &right)
{
    const QString normalizedLeft = QDir::cleanPath(QDir::fromNativeSeparators(left));
    const QString normalizedRight = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
#else
    return normalizedLeft == normalizedRight;
#endif
}
} // namespace

DirectoryProxyModel::DirectoryProxyModel(QObject *parent) : QSortFilterProxyModel(parent)
{
}

void DirectoryProxyModel::setFilterText(const QString &text)
{
    m_filterText = text;
    invalidateFilter();
}

bool DirectoryProxyModel::hasAcceptedDescendant(const QModelIndex &sourceParent) const
{
    QFileSystemModel *fsModel = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fsModel)
        return false;

    const int rows = fsModel->rowCount(sourceParent);
    for (int r = 0; r < rows; ++r)
    {
        const QModelIndex child = fsModel->index(r, 0, sourceParent);
        if (!child.isValid() || !fsModel->isDir(child))
            continue;
        if (filterAcceptsRow(r, sourceParent))
            return true;
        if (hasAcceptedDescendant(child))
            return true;
    }
    return false;
}

bool DirectoryProxyModel::filterAcceptsRow(int sourceRow, const QModelIndex &sourceParent) const
{
    QFileSystemModel *fsModel = qobject_cast<QFileSystemModel *>(sourceModel());
    if (!fsModel)
        return true;

    const QModelIndex index = fsModel->index(sourceRow, 0, sourceParent);
    if (!index.isValid())
        return false;

    // Show only directories.
    if (!fsModel->isDir(index))
        return false;

    // No text filter → accept all directories.
    if (m_filterText.isEmpty())
        return true;

    // P0: Directory name text filter with ancestor-chain preservation:
    // Accept if (a) the directory name contains the filter text, OR
    //            (b) any descendant directory matches (preserving the path chain).
    const QString name = fsModel->fileName(index);
    if (name.contains(m_filterText, Qt::CaseInsensitive))
        return true;

    return hasAcceptedDescendant(index);
}

DirectoryTree::DirectoryTree(QWidget *parent) : QTreeView(parent)
{
    m_model = new QFileSystemModel(this);
    m_model->setFilter(QDir::Dirs | QDir::Drives | QDir::NoDotAndDotDot);
    // An empty root exposes the computer level on Windows, including every
    // available drive, instead of pinning the tree to the user's home path.
    m_model->setRootPath(QString());

    m_proxy = new DirectoryProxyModel(this);
    m_proxy->setSourceModel(m_model);

    setModel(m_proxy);
    setRootIndex(QModelIndex());
    setHeaderHidden(true);
    setColumnHidden(1, true);
    setColumnHidden(2, true);
    setColumnHidden(3, true);
    setAnimated(true);
    setIndentation(15);

    // A-1.3: current-directory highlight — use a custom style so the active
    // node is visually distinct from the regular selection.
    // Keep selection colors owned by the active Qt palette so light and dark
    // application themes remain legible.
    setStyleSheet("DirectoryTree::item { padding: 2px 0; }");

    // A-1.4: QFileSystemWatcher for auto-refresh when the file system changes.
    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &DirectoryTree::onDirectoryChanged);
    connect(m_model, &QFileSystemModel::directoryLoaded, this,
            &DirectoryTree::onDirectoryLoaded);

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
                    cancelPendingNavigation();
                    m_currentPath = path;
                    watchPath(path);
                    emit directoryChanged(path);
                }
            });

    // A-1.6: loading indicator label (shown briefly while expanding large dirs).
    m_loadingLabel = new QLabel("  加载中...", this);
    m_loadingLabel->setStyleSheet("color: #888; font-style: italic; padding: 4px;");
    m_loadingLabel->hide();

    // P0: Directory name filter (placed above tree by caller via filterEdit()).
    m_filterEdit = new QLineEdit(this);
    m_filterEdit->setPlaceholderText("搜索目录...");
    m_filterEdit->setClearButtonEnabled(true);
    m_filterEdit->setTextMargins(6, 4, 6, 4);
    connect(m_filterEdit, &QLineEdit::textChanged, m_proxy, &DirectoryProxyModel::setFilterText);

    m_navigationRetryTimer = new QTimer(this);
    m_navigationRetryTimer->setSingleShot(true);
    connect(m_navigationRetryTimer, &QTimer::timeout, this,
            [this]() { tryNavigateToPending(m_navigationRequestId); });
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
            cancelPendingNavigation();
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
        cancelPendingNavigation();
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
    if (path.isEmpty())
        return;
    const QString normalized = QDir::fromNativeSeparators(QDir::cleanPath(path));
    if (normalized.isEmpty() || !QFileInfo(normalized).isDir())
        return;

    cancelPendingNavigation();
    m_pendingNavigationPath = normalized;
    m_pendingNavigationEmitSignal = emitSignal;

    // Programmatic navigation must remain possible even while a name filter is
    // active. Clearing it avoids selecting a visible ancestor while hiding the
    // requested directory.
    if (!m_filterEdit->text().isEmpty())
        m_filterEdit->clear();

    tryNavigateToPending(m_navigationRequestId);
}

void DirectoryTree::onDirectoryLoaded(const QString &path)
{
    Q_UNUSED(path);
    if (!m_pendingNavigationPath.isEmpty())
        tryNavigateToPending(m_navigationRequestId);
}

void DirectoryTree::tryNavigateToPending(quint64 requestId)
{
    if (requestId != m_navigationRequestId || m_pendingNavigationPath.isEmpty())
        return;

    const QString targetPath = m_pendingNavigationPath;
    const QModelIndex sourceIdx = sourceIndexForPath(targetPath);
    const QString indexedPath = sourceIdx.isValid() ? QDir::fromNativeSeparators(
                                                       m_model->filePath(sourceIdx))
                                                   : QString();
    if (!sourceIdx.isValid() || !m_model->isDir(sourceIdx) ||
        !equivalentPath(indexedPath, targetPath))
    {
        scheduleNavigationRetry(requestId);
        return;
    }

    // A filtered proxy may not expose an otherwise valid source index. The
    // caller normally clears the filter before reaching here; retrying keeps
    // this safe if the model is still processing that invalidation.
    const QModelIndex proxyIdx = m_proxy->mapFromSource(sourceIdx);
    if (!proxyIdx.isValid())
    {
        scheduleNavigationRetry(requestId);
        return;
    }

    m_navigationRetryTimer->stop();
    expandAncestors(sourceIdx);

    const int rowCount = m_model->rowCount(sourceIdx);
    const bool needsFetch =
        rowCount >= kLargeDirThreshold || (rowCount == 0 && m_model->canFetchMore(sourceIdx));
    if (needsFetch)
    {
        setLoading(true);
        scheduleFetchMore(sourceIdx);
    }

    m_currentPath = targetPath;
    watchPath(targetPath);

    // setCurrentIndex is signal-blocked to avoid turning programmatic sync
    // into a second directoryChanged/navigation cycle.
    QSignalBlocker selectionBlocker(selectionModel());
    setCurrentIndex(proxyIdx);
    scrollTo(proxyIdx, PositionAtCenter);
    expand(proxyIdx);
    selectionBlocker.unblock();
    applyCurrentHighlight(proxyIdx);

    const bool shouldEmit = m_pendingNavigationEmitSignal;
    m_pendingNavigationPath.clear();
    m_pendingNavigationEmitSignal = false;
    if (shouldEmit)
        emit directoryChanged(targetPath);

    if (!needsFetch)
        setLoading(false);
}

void DirectoryTree::scheduleNavigationRetry(quint64 requestId)
{
    if (requestId != m_navigationRequestId || m_pendingNavigationPath.isEmpty())
        return;
    if (!QFileInfo(m_pendingNavigationPath).isDir())
    {
        m_pendingNavigationPath.clear();
        m_pendingNavigationEmitSignal = false;
        m_navigationRetryTimer->stop();
        return;
    }
    if (!m_navigationRetryTimer->isActive())
    {
        if (m_navigationRetryCount >= kMaxNavigationRetries)
        {
            m_pendingNavigationPath.clear();
            m_pendingNavigationEmitSignal = false;
            return;
        }
        ++m_navigationRetryCount;
        const int backoff = kInitialNavigationRetryDelayMs
                            << qMin(m_navigationRetryCount - 1, 4);
        m_navigationRetryTimer->start(qMin(backoff, kMaxNavigationRetryDelayMs));
    }
}

void DirectoryTree::cancelPendingNavigation()
{
    ++m_navigationRequestId;
    m_pendingNavigationPath.clear();
    m_pendingNavigationEmitSignal = false;
    m_navigationRetryCount = 0;
    if (m_navigationRetryTimer)
        m_navigationRetryTimer->stop();
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
        if (equivalentPath(path, m_currentPath))
            emit directoryChanged(path);
    }
}

void DirectoryTree::scheduleFetchMore(const QModelIndex &sourceIdx)
{
    // A-1.5: progressive fetch — call fetchMore once, then re-schedule if the
    // model still has more children. Yields to the event loop between batches
    // so expand/navigate stays interactive on 10k+ entry directories.
    if (!sourceIdx.isValid())
        return;
    const QString path = m_model->filePath(sourceIdx);
    m_pendingFetchPath = path;
    if (m_model->canFetchMore(sourceIdx))
        m_model->fetchMore(sourceIdx);

    // Re-check after the model has processed the batch.
    QTimer::singleShot(0, this,
                       [this, path]()
                       {
                           if (m_pendingFetchPath != path)
                               return; // superseded by a newer navigate
                           const QModelIndex src = m_model->index(path);
                           if (!src.isValid())
                           {
                               setLoading(false);
                               m_pendingFetchPath.clear();
                               return;
                           }
                           if (m_model->canFetchMore(src))
                           {
                               setLoading(true);
                               scheduleFetchMore(src);
                           }
                           else
                           {
                               setLoading(false);
                               m_pendingFetchPath.clear();
                           }
                       });
}

void DirectoryTree::onRowsInserted(const QModelIndex &parent, int first, int last)
{
    Q_UNUSED(first);
    Q_UNUSED(last);
    // A-1.5: when children appear after fetchMore, clear loading state
    // only if no more progressive fetch is pending.
    if (m_loading && m_pendingFetchPath.isEmpty())
    {
        const QString parentPath = m_model->filePath(parent);
        if (equivalentPath(parentPath, m_currentPath) || equivalentPath(parentPath, m_watchedPath))
            setLoading(false);
    }
    if (!m_pendingNavigationPath.isEmpty())
        tryNavigateToPending(m_navigationRequestId);
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

    // A-1.5: large dirs use progressive fetch so expand stays interactive.
    if (m_model->canFetchMore(source) || m_model->rowCount(source) >= kLargeDirThreshold)
    {
        setLoading(true);
        scheduleFetchMore(source);
    }
    else
    {
        m_model->fetchMore(source);
    }
    watchPath(path);
}

void DirectoryTree::watchPath(const QString &path)
{
    // A-1.4: add the path to the file system watcher so we get notified when
    // Explorer creates/deletes subdirectories.
    if (path.isEmpty() || equivalentPath(path, m_watchedPath))
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
        if (equivalentPath(path, m_currentPath) && !m_currentPath.isEmpty())
        {
            // Draw accent background.
            painter->save();
            QColor accent = palette().color(QPalette::Highlight);
            accent.setAlpha(40);
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
