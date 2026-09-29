// Deferred directory-tree highlight. QFileSystemModel::index(fullPath) with an
// empty setRootPath walks every uncached ancestor on the calling thread. That
// work is one prefix per event-loop turn, and it waits until the gallery has
// painted (or a tree-only fallback) so the first thumbnail batch is not stuck
// behind it.
#include "directorytree.h"

#include <QCoreApplication>
#include <QDir>
#include <QFileInfo>
#include <QFileSystemModel>
#include <QMetaObject>
#include <QPointer>
#include <QSignalBlocker>
#include <QTimer>

#include <QtConcurrent/QtConcurrent>

#include <atomic>
#include <functional>
#include <memory>

namespace
{
constexpr int kLargeDirThreshold = 500;
// Tree-only callers have no gallery nudge. A real open() nudges from the first
// batch long before this, so the model index stays behind that paint.
constexpr int kHighlightFallbackMs = 1500;

std::atomic<std::shared_ptr<const std::function<void()>>> &navigationIndexProbeRef()
{
    static std::atomic<std::shared_ptr<const std::function<void()>>> hook;
    return hook;
}

void invokeNavigationIndexProbe()
{
    const auto probe = navigationIndexProbeRef().load(std::memory_order_acquire);
    if (!probe)
        return;
    try
    {
        (*probe)();
    }
    catch (...)
    {
    }
}

void appendPrefix(QStringList &prefixes, const QString &acc)
{
    if (prefixes.isEmpty() || prefixes.last() != acc)
        prefixes.append(acc);
}

// Shortest path first, so each index() adds one uncached component instead of
// stating the whole chain in a single GUI call. UNC keeps the double slash.
QStringList pathPrefixes(const QString &path)
{
    const QString clean = QDir::fromNativeSeparators(QDir::cleanPath(path));
    if (clean.isEmpty())
        return {};
    const bool unc = clean.startsWith(QLatin1String("//"));
    const bool rooted = !unc && clean.startsWith(QLatin1Char('/'));
    QStringList prefixes;
    QString acc;
    if (unc)
        acc = QStringLiteral("//");
    else if (rooted)
    {
        acc = QStringLiteral("/");
        prefixes.append(acc);
    }
    const QStringList parts = clean.split(QLatin1Char('/'), Qt::SkipEmptyParts);
    for (int i = 0; i < parts.size(); ++i)
    {
        const QString &part = parts.at(i);
        if (acc.isEmpty())
            acc = part;
        else if (acc.endsWith(QLatin1Char('/')))
            acc += part;
        else
            acc += QLatin1Char('/') + part;
        if (!unc && i == 0 && acc.size() == 2 && acc.at(1) == QLatin1Char(':'))
            acc += QLatin1Char('/');
        appendPrefix(prefixes, acc);
    }
    return prefixes;
}
} // namespace

void DirectoryTree::setNavigationIndexProbeHook(const std::function<void()> &hook)
{
    std::shared_ptr<const std::function<void()>> next;
    if (hook)
        next = std::make_shared<const std::function<void()>>(hook);
    navigationIndexProbeRef().store(std::move(next), std::memory_order_release);
}

void DirectoryTree::scheduleDeferredNavigation(quint64 requestId)
{
    if (requestId != m_navigationRequestId || m_pendingNavigationPath.isEmpty())
        return;
    if (!m_navigationStatReady || !m_highlightStarted || m_deferredNavigationQueued)
        return;
    m_deferredNavigationQueued = true;
    // Two turns: the gallery's rowsInserted posts a 0ms layout timer before
    // this one. That layout posts the paint. The second timer runs after that
    // paint, so index() cannot sit in the same timer batch.
    QTimer::singleShot(0, this, [this, requestId]() {
        if (requestId != m_navigationRequestId)
        {
            m_deferredNavigationQueued = false;
            return;
        }
        QTimer::singleShot(0, this, [this, requestId]() {
            m_deferredNavigationQueued = false;
            if (requestId != m_navigationRequestId)
                return;
            tryNavigateToPending(requestId);
        });
    });
}

void DirectoryTree::armHighlightFallback(quint64 requestId)
{
    QTimer::singleShot(kHighlightFallbackMs, this, [this, requestId]() {
        if (requestId != m_navigationRequestId || m_highlightStarted)
            return;
        if (!m_navigationStatReady || m_pendingNavigationPath.isEmpty())
            return;
        m_highlightStarted = true;
        scheduleDeferredNavigation(requestId);
    });
}

void DirectoryTree::nudgeDeferredHighlight()
{
    if (m_pendingNavigationPath.isEmpty())
        return;
    if (!m_navigationStatReady)
    {
        m_highlightNudgePending = true;
        return;
    }
    if (!m_highlightStarted)
    {
        m_highlightNudgePending = false;
        m_highlightStarted = true;
    }
    scheduleDeferredNavigation(m_navigationRequestId);
}

void DirectoryTree::acceptNavigationStat(quint64 requestId, bool isDir)
{
    if (requestId != m_navigationRequestId)
        return;
    if (!isDir)
    {
        m_pendingNavigationPath.clear();
        m_pendingNavigationEmitSignal = false;
        m_navigationStatReady = false;
        if (m_navigationRetryTimer)
            m_navigationRetryTimer->stop();
        return;
    }
    m_navigationStatReady = true;
    m_navigationSegment = 0;
    m_navigationPrefixes = pathPrefixes(m_pendingNavigationPath);
    m_highlightStarted = false;
    if (m_highlightNudgePending || !m_holdHighlightForGallery)
    {
        m_highlightNudgePending = false;
        m_highlightStarted = true;
        scheduleDeferredNavigation(requestId);
        return;
    }
    armHighlightFallback(requestId);
}

void DirectoryTree::tryNavigateToPending(quint64 requestId)
{
    if (requestId != m_navigationRequestId || m_pendingNavigationPath.isEmpty())
        return;
    if (!m_navigationStatReady || !m_highlightStarted)
        return;

    // One uncached prefix per turn. index(fullPath) with setRootPath("") walks
    // every ancestor, including the icon stat, before the event loop can paint.
    if (m_navigationSegment < m_navigationPrefixes.size())
    {
        const QString prefix = m_navigationPrefixes.at(m_navigationSegment);
        const QModelIndex sourceIdx = sourceIndexForPath(prefix);
        if (!sourceIdx.isValid() || !m_model->isDir(sourceIdx))
        {
            scheduleNavigationRetry(requestId);
            return;
        }
        ++m_navigationSegment;
        const QModelIndex proxyIdx = m_proxy->mapFromSource(sourceIdx);
        if (proxyIdx.isValid())
            expand(proxyIdx);
        scheduleDeferredNavigation(requestId);
        return;
    }
    finishPendingNavigation(requestId);
}

void DirectoryTree::finishPendingNavigation(quint64 requestId)
{
    if (requestId != m_navigationRequestId || m_pendingNavigationPath.isEmpty())
        return;

    const QString targetPath = m_pendingNavigationPath;
    const QModelIndex sourceIdx = sourceIndexForPath(targetPath);
    const QString indexedPath =
        sourceIdx.isValid() ? QDir::fromNativeSeparators(m_model->filePath(sourceIdx)) : QString();
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
    m_navigationPrefixes.clear();
    m_navigationSegment = 0;
    if (shouldEmit)
        emit directoryChanged(targetPath);

    if (!needsFetch)
        setLoading(false);
}

void DirectoryTree::resolvePendingNavigation(quint64 requestId)
{
    if (requestId != m_navigationRequestId || m_pendingNavigationPath.isEmpty())
        return;
    const QString target = m_pendingNavigationPath;
    const QPointer<DirectoryTree> self(this);
    (void)QtConcurrent::run(
        [self, requestId, target]()
        {
            bool isDir = false;
            try
            {
                isDir = QFileInfo(target).isDir();
            }
            catch (...)
            {
                isDir = false;
            }
            // Stand-in for the slow QFileSystemModel::index. It stays on this
            // worker so a multi-second stat cannot sit ahead of applyScanBatch.
            if (isDir)
                invokeNavigationIndexProbe();
            if (!qApp)
                return;
            QMetaObject::invokeMethod(qApp, [self, requestId, isDir]() {
                if (!self)
                    return;
                self->acceptNavigationStat(requestId, isDir);
            });
        });
}
