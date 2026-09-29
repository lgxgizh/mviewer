// Background latency probe and scan-stall watchdog for ThumbnailPanel.
// QStorageInfo / GetDriveTypeW and a hung directory iterator must not run on
// the GUI thread or hold the busy cursor until the process exits.
#include "thumbnailpanel_p.h"

#include <QtConcurrent/QtConcurrent>

#include <QTimer>

namespace
{
std::atomic<std::shared_ptr<const std::function<void()>>> &highLatencyProbeHookRef()
{
    static std::atomic<std::shared_ptr<const std::function<void()>>> hook;
    return hook;
}

// No directory entries for this long means the scan is stuck, not merely large.
// NOLINTNEXTLINE(readability-magic-numbers)
constexpr int kScanStallMs = 8000;
} // namespace

void ThumbnailPanel::setHighLatencyProbeHook(const std::function<void()> &hook)
{
    std::shared_ptr<const std::function<void()>> next;
    if (hook)
        next = std::make_shared<const std::function<void()>>(hook);
    highLatencyProbeHookRef().store(std::move(next), std::memory_order_release);
}

bool ThumbnailPanel::lexicalHighLatencyHint(const QString &path)
{
    return path.startsWith(QLatin1String("\\\\")) || path.startsWith(QLatin1String("//"));
}

void ThumbnailPanel::releaseScanCursor(const std::shared_ptr<std::atomic<int>> &refs,
                                       const std::shared_ptr<std::atomic<bool>> &released)
{
    if (!released)
        return;
    bool expected = false;
    if (!released->compare_exchange_strong(expected, true, std::memory_order_acq_rel))
        return;
    marshalBusyRestore(refs);
}

void ThumbnailPanel::scheduleHighLatencyProbe(const QString &path, int gen)
{
    if (path.isEmpty() || lexicalHighLatencyHint(path))
        return;
    const QPointer<ThumbnailPanel> self(this);
    const auto hook = highLatencyProbeHookRef().load(std::memory_order_acquire);
    (void)QtConcurrent::run(
        [self, path, gen, hook]()
        {
            bool high = false;
            bool hookOk = true;
            if (hook)
            {
                try
                {
                    (*hook)();
                }
                catch (...)
                {
                    hookOk = false;
                }
            }
            if (hookOk)
            {
                try
                {
                    high = isHighLatencyBrowsePath(path);
                }
                catch (...)
                {
                    high = false;
                }
            }
            if (!qApp)
                return;
            QMetaObject::invokeMethod(qApp,
                                      [self, gen, high]()
                                      {
                                          if (!self || self->m_dirGen != gen ||
                                              self->m_highLatencyDir == high)
                                              return;
                                          self->m_highLatencyDir = high;
                                          self->updateVisibleRange();
                                      });
        });
}

void ThumbnailPanel::armScanWatchdog(int gen)
{
    const uint64_t serial = ++m_scanWatchSerial;
    const auto refs = m_busyCursorRefs;
    const auto released = m_scanCursorReleased;
    QTimer::singleShot(kScanStallMs, this,
                       [this, gen, serial, refs, released]()
                       {
                           if (serial != m_scanWatchSerial || gen != m_dirGen || m_scanComplete)
                               return;
                           releaseScanCursor(refs, released);
                           if (!m_allEntries.isEmpty())
                               return;
                           m_scanStallAnnounced = true;
                           emit browseStatusChanged(QStringLiteral("目录加载失败"));
                       });
}

void ThumbnailPanel::noteScanProgress(int gen)
{
    if (gen != m_dirGen || m_scanComplete)
        return;
    const bool resumed = m_scanStallAnnounced;
    m_scanStallAnnounced = false;
    armScanWatchdog(gen);
    if (resumed)
        emit browseStatusChanged(QStringLiteral("正在扫描…"));
}
