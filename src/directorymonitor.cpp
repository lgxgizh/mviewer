#include "directorymonitor.h"

#include <QDir>
#include <QMetaObject>

#include <QtConcurrent/QtConcurrent>

namespace
{
constexpr int kDebounceMs = 75;
constexpr int kStabilityWindowMs = 100;
constexpr int kMaxStabilityRetries = 3;

QString normalized(const QString &path)
{
    return QDir::cleanPath(QDir::fromNativeSeparators(path));
}

bool sameSnapshot(const mviewer::core::DirectorySnapshot &a,
                  const mviewer::core::DirectorySnapshot &b)
{
    return a.path == b.path && a.available == b.available && a.entries == b.entries &&
           a.sidecars == b.sidecars;
}

QString parentDirectory(const QString &normalizedPath)
{
    const int slash = normalizedPath.lastIndexOf(QLatin1Char('/'));
    if (slash < 0)
        return {};
    if (slash == 0)
        return QStringLiteral("/");
    QString parent = normalizedPath.left(slash);
    // "C:/photos" -> "C:/", which QFileSystemWatcher can monitor.
    if (parent.size() == 2 && parent.at(1) == QLatin1Char(':'))
        parent += QLatin1Char('/');
    return parent;
}
} // namespace

DirectoryMonitor::DirectoryMonitor(QObject *parent) : QObject(parent)
{
    m_alive = std::make_shared<std::atomic<bool>>(true);
    m_generation = std::make_shared<std::atomic<uint64_t>>(0);
    m_pool.setMaxThreadCount(1);

    m_watcher = new QFileSystemWatcher(this);
    connect(m_watcher, &QFileSystemWatcher::directoryChanged, this,
            &DirectoryMonitor::onWatcherDirectoryChanged);

    m_debounce = new QTimer(this);
    m_debounce->setSingleShot(true);
    connect(m_debounce, &QTimer::timeout, this, &DirectoryMonitor::onDebounceTimeout);

    m_stabilityTimer = new QTimer(this);
    m_stabilityTimer->setSingleShot(true);
    connect(m_stabilityTimer, &QTimer::timeout, this, &DirectoryMonitor::onStabilityTimeout);
}

DirectoryMonitor::~DirectoryMonitor()
{
    m_alive->store(false, std::memory_order_release);
    m_generation->fetch_add(1, std::memory_order_acq_rel);
    if (m_debounce)
        m_debounce->stop();
    if (m_stabilityTimer)
        m_stabilityTimer->stop();
    m_pool.clear();
    // Do not block the GUI thread waiting for a possibly slow directory
    // snapshot. Running tasks check m_alive and self-cancel safely.
}

void DirectoryMonitor::setActiveDirectory(const QString &path)
{
    if (path == m_activePath && !m_activePath.isEmpty())
    {
        ensureActivePathWatched();
        return;
    }
    const QString next = normalized(path);
    if (next == m_activePath && !m_activePath.isEmpty())
    {
        ensureActivePathWatched();
        return;
    }

    m_generation->fetch_add(1, std::memory_order_acq_rel);
    m_activePath = next;
    m_committed = {};
    m_committed.path = next.toUtf8().toStdString();
    m_stabilityCandidate = {};
    m_stabilityDelta = {};
    m_hasStabilityCandidate = false;
    m_scanInFlight = false;
    m_dirtyAgain = false;
    m_waitingForStability = false;
    m_stabilityRetries = 0;
    m_debounce->stop();
    m_stabilityTimer->stop();
    m_fallbackWatch.clear();
    const QStringList watchedDirectories = m_watcher->directories();
    if (!watchedDirectories.isEmpty())
        m_watcher->removePaths(watchedDirectories);
    ensureActivePathWatched();
    if (!m_activePath.isEmpty())
        startReconcile(); // initial baseline; it emits no mutation delta
}

void DirectoryMonitor::notifyDirectoryChanged(const QString &path)
{
    if (m_activePath.isEmpty())
        return;
    if (path != m_activePath && normalized(path) != m_activePath)
        return;
    // A deletion hint has to arm the parent watch before the snapshot returns,
    // or the folder can be recreated during that window with nobody watching.
    ensureActivePathWatched();
    ++m_watcherHintCount;
    scheduleReconcile(kDebounceMs);
}

void DirectoryMonitor::reconcileNow()
{
    if (m_activePath.isEmpty())
        return;
    ++m_watcherHintCount;
    m_debounce->stop();
    startReconcile();
}

void DirectoryMonitor::onWatcherDirectoryChanged(const QString &path)
{
    // QFileSystemWatcher drops a directory after it disappears. Re-add it when
    // it comes back. While it is gone, a parent hint means the child may have
    // been recreated; capture that before ensureActivePathWatched drops the
    // fallback watch on a successful re-add.
    const QString hinted = normalized(path);
    const bool parentHint = !m_fallbackWatch.isEmpty() && hinted == m_fallbackWatch;
    ensureActivePathWatched();
    if (hinted == m_activePath)
    {
        notifyDirectoryChanged(path);
        return;
    }
    if (parentHint)
        scheduleReconcile(kDebounceMs);
}

void DirectoryMonitor::scheduleReconcile(int delayMs)
{
    if (m_debounce->isActive())
        return;
    m_debounce->start(delayMs);
}

void DirectoryMonitor::onDebounceTimeout()
{
    startReconcile();
}

void DirectoryMonitor::onStabilityTimeout()
{
    if (!m_waitingForStability)
        return;
    m_waitingForStability = false;
    startReconcile();
}

void DirectoryMonitor::startReconcile()
{
    if (m_activePath.isEmpty())
        return;
    if (m_scanInFlight)
    {
        m_dirtyAgain = true;
        return;
    }

    m_scanInFlight = true;
    const QString path = m_activePath;
    const uint64_t generation = m_generation->load(std::memory_order_acquire);
    const auto alive = m_alive;
    const auto generationToken = m_generation;
    (void)QtConcurrent::run(
        &m_pool,
        [this, path, generation, alive, generationToken]
        {
            if (!alive->load(std::memory_order_acquire) ||
                generationToken->load(std::memory_order_acquire) != generation)
                return;
            auto snapshot = mviewer::core::snapshotDirectory(path.toUtf8().toStdString(), generation);
            QMetaObject::invokeMethod(
                this,
                [this, alive, generation, generationToken,
                 snapshot = std::move(snapshot)]() mutable
                {
                    if (!alive->load(std::memory_order_acquire) ||
                        generation != generationToken->load(std::memory_order_acquire))
                        return;
                    handleSnapshot(std::move(snapshot), generation);
                },
                Qt::QueuedConnection);
        });
}

void DirectoryMonitor::handleSnapshot(mviewer::core::DirectorySnapshot snapshot,
                                      uint64_t generation)
{
    if (generation != m_generation->load(std::memory_order_acquire))
        return;
    m_scanInFlight = false;
    ++m_snapshotScanCount;
    ensureActivePathWatched();
    // addPath can still list a directory Qt has already dropped. Arm the
    // parent from the snapshot, which is the source of truth.
    if (!snapshot.available)
        armFallbackWatch();

    if (m_committed.path.empty())
        m_committed.path = snapshot.path;
    const bool initial = m_committed.generation == 0 && !m_committed.available &&
                         m_committed.entries.empty() && m_committed.sidecars.empty();
    mviewer::core::DirectoryDelta delta =
        mviewer::core::diffDirectorySnapshots(m_committed, snapshot);
    if (initial)
    {
        snapshot.generation = generation;
        m_committed = std::move(snapshot);
        emit directoryAvailabilityChanged(QString::fromUtf8(m_committed.path.data()),
                                          m_committed.available);
    }
    else if (!delta.empty())
    {
        if (!m_hasStabilityCandidate)
        {
            m_stabilityCandidate = snapshot;
            m_stabilityDelta = delta;
            m_hasStabilityCandidate = true;
            m_stabilityRetries = 0;
        }
        else if (sameSnapshot(snapshot, m_stabilityCandidate))
        {
            commitSnapshot(snapshot, m_stabilityDelta);
            m_hasStabilityCandidate = false;
            m_stabilityRetries = 0;
        }
        else
        {
            m_stabilityCandidate = snapshot;
            m_stabilityDelta = delta;
            ++m_stabilityRetries;
        }

        if (m_hasStabilityCandidate)
        {
            if (m_stabilityRetries >= kMaxStabilityRetries)
            {
                commitSnapshot(m_stabilityCandidate, m_stabilityDelta);
                m_hasStabilityCandidate = false;
                m_stabilityRetries = 0;
            }
            else
            {
                ++m_stabilityRetryCount;
                m_waitingForStability = true;
                m_stabilityTimer->start(kStabilityWindowMs);
            }
        }
    }

    if (m_dirtyAgain)
    {
        m_dirtyAgain = false;
        scheduleReconcile(kDebounceMs);
    }
}

void DirectoryMonitor::commitSnapshot(const mviewer::core::DirectorySnapshot &snapshot,
                                      const mviewer::core::DirectoryDelta &delta)
{
    const bool availabilityChanged = m_committed.available != snapshot.available;
    mviewer::core::DirectorySnapshot committed = snapshot;
    committed.generation = m_generation->load(std::memory_order_acquire);
    m_committed = committed;
    ++m_deltaCount;
    if (availabilityChanged)
        emit directoryAvailabilityChanged(QString::fromUtf8(committed.path.data()),
                                          committed.available);
    if (!delta.empty())
        emit directoryDeltaReady(delta);
}

void DirectoryMonitor::dropFallbackWatch()
{
    if (m_fallbackWatch.isEmpty() || !m_watcher)
        return;
    const QString parent = m_fallbackWatch;
    m_fallbackWatch.clear();
    if (m_watcher->directories().contains(parent))
        m_watcher->removePath(parent);
}

void DirectoryMonitor::armFallbackWatch()
{
    if (m_activePath.isEmpty() || !m_watcher)
        return;
    const QString parent = parentDirectory(m_activePath);
    if (parent.isEmpty() || parent == m_activePath)
        return;
    if (m_fallbackWatch != parent)
        dropFallbackWatch();
    m_fallbackWatch = parent;
    if (!m_watcher->directories().contains(parent))
        m_watcher->addPath(parent);
}

void DirectoryMonitor::ensureActivePathWatched()
{
    // Best-effort. QDir::exists on the GUI thread is what stalls the
    // directoryChanged stack on a slow or disconnected volume. addPath fails
    // quietly when the path is missing. Watch the parent in that case so
    // recreating the folder is visible without a manual refresh.
    if (m_activePath.isEmpty() || !m_watcher)
        return;
    if (!m_watcher->directories().contains(m_activePath))
        m_watcher->addPath(m_activePath);
    if (m_watcher->directories().contains(m_activePath))
    {
        dropFallbackWatch();
        return;
    }
    armFallbackWatch();
}
