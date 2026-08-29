#pragma once

#include "core/filesystem/DirectorySnapshot.h"

#include <QThreadPool>
#include <QTimer>
#include <QFileSystemWatcher>
#include <QObject>
#include <QString>

#include <atomic>

// M56: active-browse directory monitor.  QFileSystemWatcher is only a hint
// source; this class owns debouncing, bounded async snapshots, stability
// retries and latest-wins delivery.  It never touches a widget or a model.
class DirectoryMonitor : public QObject
{
    Q_OBJECT

  public:
    explicit DirectoryMonitor(QObject *parent = nullptr);
    ~DirectoryMonitor() override;

    void setActiveDirectory(const QString &path);
    void notifyDirectoryChanged(const QString &path);
    void reconcileNow();

    QString activeDirectory() const
    {
        return m_activePath;
    }
    int watcherHintCount() const
    {
        return m_watcherHintCount;
    }
    int snapshotScanCount() const
    {
        return m_snapshotScanCount;
    }
    int deltaCount() const
    {
        return m_deltaCount;
    }
    int stabilityRetryCount() const
    {
        return m_stabilityRetryCount;
    }
    const mviewer::core::DirectorySnapshot &snapshot() const
    {
        return m_committed;
    }

  signals:
    // Immutable/value-style data: consumers apply this on the GUI thread.
    void directoryDeltaReady(const mviewer::core::DirectoryDelta &delta);
    void directoryAvailabilityChanged(const QString &path, bool available);

  private slots:
    void onWatcherDirectoryChanged(const QString &path);
    void onDebounceTimeout();
    void onStabilityTimeout();

  private:
    void scheduleReconcile(int delayMs);
    void startReconcile();
    void handleSnapshot(mviewer::core::DirectorySnapshot snapshot, uint64_t generation);
    void commitSnapshot(const mviewer::core::DirectorySnapshot &snapshot,
                        const mviewer::core::DirectoryDelta &delta);
    void ensureActivePathWatched();

    QFileSystemWatcher *m_watcher = nullptr;
    QTimer *m_debounce = nullptr;
    QTimer *m_stabilityTimer = nullptr;
    QThreadPool m_pool;
    std::shared_ptr<std::atomic<bool>> m_alive;
    std::shared_ptr<std::atomic<uint64_t>> m_generation;

    QString m_activePath;
    mviewer::core::DirectorySnapshot m_committed;
    mviewer::core::DirectorySnapshot m_stabilityCandidate;
    mviewer::core::DirectoryDelta m_stabilityDelta;
    bool m_hasStabilityCandidate = false;
    bool m_scanInFlight = false;
    bool m_dirtyAgain = false;
    bool m_waitingForStability = false;
    int m_stabilityRetries = 0;
    int m_watcherHintCount = 0;
    int m_snapshotScanCount = 0;
    int m_deltaCount = 0;
    int m_stabilityRetryCount = 0;
};
