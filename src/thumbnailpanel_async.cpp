// M46: ThumbnailPanel async-scan TU (ADR-014 responsibility split). The
// directory-scan worker, the dimension-probe worker, the UI-side busy-cursor
// refcount and the deterministic scan-iteration probe live here so the core
// thumbnailpanel.cpp stays under the 800-line guard while the cooperative
// supersession/cancellation machinery stays cohesive.
#include "browseformats.h"
#include "thumbnailpanel_p.h"

#include "core/image/ImageFormats.h"
#include "core/image/QtConvert.h"
#include "core/image/QtMetadataSemantics.h"
#include "core/metadata/MetadataIndexer.h"

#include <QtConcurrent/QtConcurrent>

#include <QStorageInfo>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QDateTime>
#include <QSet>
#include <QThread>
#include <algorithm>
#include <functional>

namespace
{
// M46/M55 test instrumentation storage (empty in production). The immutable
// shared callback is atomically swapped. A scan takes one snapshot, so the
// production no-probe path is a single null check per iteration with no mutex,
// while install/reset can safely race an in-flight worker.
std::shared_ptr<const std::function<void()>> &scanIterationProbeRef()
{
    static std::shared_ptr<const std::function<void()>> probe;
    return probe;
}
} // namespace

void ThumbnailPanel::setScanIterationProbe(const std::function<void()> &probe)
{
    std::shared_ptr<const std::function<void()>> next;
    if (probe)
        next = std::make_shared<const std::function<void()>>(probe);
    std::atomic_store_explicit(&scanIterationProbeRef(), std::move(next),
                               std::memory_order_release);
}

void ThumbnailPanel::invokeScanProbe()
{
    const auto probe = scanIterationProbeSnapshot();
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

std::shared_ptr<const std::function<void()>> ThumbnailPanel::scanIterationProbeSnapshot()
{
    return std::atomic_load_explicit(&scanIterationProbeRef(), std::memory_order_acquire);
}

// M46: the app-global busy cursor is ref-counted UI-side. Every setDirectory()
// increments the panel's refcount; every scan completion/abort and the
// destructor drain decrement it exactly once, so override-cursor state can
// never leak (a queued scan dropped by m_scanPool.clear() must not strand the
// cursor). The override cursor is restored ONLY on the transition to zero:
// a superseded scan that aborts while other scans hold refs must not pop a
// cursor that is still owned by them. restoreBusyCursorOnce() must run on the
// GUI thread.
void ThumbnailPanel::restoreBusyCursorOnce(const std::shared_ptr<std::atomic<int>> &refs)
{
    if (!refs)
        return;
    int cur = refs->load(std::memory_order_acquire);
    while (cur > 0)
    {
        if (refs->compare_exchange_weak(cur, cur - 1, std::memory_order_acq_rel))
        {
            if (cur == 1)
                QApplication::restoreOverrideCursor();
            return;
        }
    }
}

void ThumbnailPanel::marshalBusyRestore(const std::shared_ptr<std::atomic<int>> &refs)
{
    if (!qApp)
        return; // app teardown: the destructor drain owns the restore
    QMetaObject::invokeMethod(qApp, [refs]() { ThumbnailPanel::restoreBusyCursorOnce(refs); });
}

// ---- M46: cooperative directory scan ----------------------------------------
namespace
{
// M23 P2: the type-filter rule, factored so the on-thread directory scan and the
// off-thread enumeration share exactly one implementation.
bool passesTypeFilter(const QString &typeFilter, const QString &suffixRaw)
{
    const QByteArray suffix = suffixRaw.toUtf8();
    const QByteArray filter = typeFilter.toUtf8();
    return mviewer::core::suffixMatchesTypeFilter(
        std::string_view(suffix.constData(), static_cast<size_t>(suffix.size())),
        std::string_view(filter.constData(), static_cast<size_t>(filter.size())));
}

// The first screen is published one file at a time so a small folder can paint
// before the walk finishes. After that, keep the large-directory batch size.
constexpr int kFirstScreenEntries = 16;
constexpr int kScanBatchEntries = 128;

// Returns true when the walk was superseded. The caller publishes only a
// finished walk; cursor release is owned by the single completion path.
bool scanProgressiveDirectory(
    const QString &path, const std::shared_ptr<std::atomic<bool>> &alive,
    const std::shared_ptr<std::atomic<uint64_t>> &genToken, int gen, const QString &typeFilter,
    QList<ThumbnailPanel::Entry> &entries,
    const std::shared_ptr<const std::function<void()>> &probe,
    const std::function<void(const QList<ThumbnailPanel::Entry> &)> &publishBatch)
{
    QList<ThumbnailPanel::Entry> batch;
    const auto supportedVec = mviewer::core::ImageFormats::supportedSuffixes();
    QSet<QString> supportedExts;
    supportedExts.reserve(static_cast<qsizetype>(supportedVec.size()));
    for (const auto &s : supportedVec)
        supportedExts.insert(QString::fromStdString(s));

    QDirIterator it(path, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                    QDirIterator::NoIteratorFlags);
    while (it.hasNext())
    {
        // M54: QDirIterator yields the first model batch before the full
        // directory is known, so the view can start painting and decoding.
        if (!alive->load() ||
            genToken->load(std::memory_order_acquire) != static_cast<uint64_t>(gen))
            return true;
        if (probe)
        {
            try
            {
                (*probe)();
            }
            catch (...)
            {
            }
        }
        it.next();
        const QFileInfo fi = it.fileInfo();
        const QString suffix = fi.suffix().toLower();
        if (suffix.isEmpty() || !supportedExts.contains(suffix))
            continue;
        const ThumbnailPanel::Entry entry{fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0,
                                          fi.lastModified()};
        entries.append(entry);
        if (entryMatchesTypeFilter(entry.path, typeFilter))
            batch.append(entry);
        const int batchLimit = (entries.size() == 1) ? 1
                                                     : (entries.size() <= kFirstScreenEntries
                                                            ? (kFirstScreenEntries - 1)
                                                            : kScanBatchEntries);
        if (batch.size() >= batchLimit)
        {
            publishBatch(batch);
            batch.clear();
            // The UI thread is what paints this first row. Yield once so a
            // tight local walk does not finish the whole folder first.
            if (entries.size() == 1)
                QThread::yieldCurrentThread();
        }
    }
    if (!batch.isEmpty())
        publishBatch(batch);
    std::sort(entries.begin(), entries.end(),
              [](const ThumbnailPanel::Entry &a, const ThumbnailPanel::Entry &b)
              { return browseOrderedLess(0, a.name, b.name, a.path, b.path, true); });
    return false;
}

void callProbe(const std::shared_ptr<const std::function<void()>> &probe)
{
    if (!probe)
        return;
    try
    {
        (*probe)();
    }
    catch (const std::exception &)
    {
        return;
    }
    catch (...)
    {
        return;
    }
}
} // namespace

bool ThumbnailPanel::isHighLatencyBrowsePath(const QString &path)
{
    if (path.isEmpty())
        return false;
    // UNC: \\server\share or //server/share (Qt often normalizes to the latter).
    if (path.startsWith(QLatin1String("\\\\")) || path.startsWith(QLatin1String("//")))
        return true;
#if defined(Q_OS_WIN)
    if (path.size() >= 2 && path.at(1) == QLatin1Char(':'))
    {
        wchar_t root[4] = {static_cast<wchar_t>(path.at(0).unicode()), L':', L'\\', L'\0'};
        const UINT type = GetDriveTypeW(root);
        if (type == DRIVE_REMOTE)
            return true;
    }
#endif
    const QStorageInfo info(path);
    if (info.isValid())
    {
        const QString fs = QString::fromLatin1(info.fileSystemType()).toLower();
        if (fs.contains(QLatin1String("nfs")) || fs.contains(QLatin1String("cifs")) ||
            fs.contains(QLatin1String("smb")) || fs.contains(QLatin1String("sshfs")) ||
            fs.contains(QLatin1String("fuse")))
            return true;
    }
    return false;
}

void ThumbnailPanel::setDirectory(const QString &path)
{
    m_currentDir = path;
    emit directorySourceChanged(m_currentDir);
    m_directoryUnavailable = false;
    m_filterText.clear();
    m_filterRecursive = false;

    // P0-1 (perf): a new directory generation. Any in-flight background
    // dimension resolve or directory scan from the previous folder is
    // invalidated by the bump. M46: the shared generation token additionally
    // makes in-flight workers STOP cooperatively (they re-check it every
    // iteration), instead of running to completion and being discarded later.
    // Bump generation BEFORE any path probing / cache work so a slow
    // isHighLatencyBrowsePath() cannot extend superseded scan iterations.
    ++m_dirGen;
    const int gen = m_dirGen;
    m_scanGenToken->store(static_cast<uint64_t>(gen), std::memory_order_release);
    // This panel's metadata index only. Search re-index is a different
    // MetadataIndexer request and must keep running until it is superseded.
    if (m_metaRequestId != 0)
    {
        mviewer::core::MetadataIndexer::instance().cancelRequest(m_metaRequestId);
        m_metaRequestId = 0;
    }
    m_metaIndexing = false;
    m_dimsResolved = false;
    // Lexical UNC hint only. QStorageInfo / GetDriveTypeW run on a worker so a
    // stale volume cannot freeze this call before the scan starts.
    m_highLatencyDir = lexicalHighLatencyHint(path);
    m_scanStallAnnounced = false;
    m_scanCursorReleased = std::make_shared<std::atomic<bool>>(false);
    ThumbnailProvider::clearSourceIdentityHints();
    m_scanComplete = false;
    m_galleryModelReadyEmitted = false;
    const bool heavyFilter = hasMetadataBrowseFilter(m_filterText, m_recentFilter, m_cameraFilter,
                                                     m_lensFilter, m_isoFilter);
    m_scanProgressive = streamableBrowseSort(m_sortMode) && !heavyFilter;

    // M23 P2 (first-screen): paint the (empty) directory shell immediately so a
    // 1000-image folder shows its grid in well under 1s, then scan the disk off
    // the UI thread and stream the real entries in once they are ready.
    resetDirectoryState();
    m_model->setStringList({});
    // Supersede thumbnail demand at the same T0 as the directory transition;
    // discovered batches below then append into an empty, current source set.
    ThumbnailPipeline::instance().setSourcesForConsumer(m_consumerId, {});
    viewport()->update();
    // Publish the empty shell immediately so consumers drop the previous
    // directory sequence before this directory's worker result arrives.
    emit sequenceChanged(m_currentDir, {});
    emit statsChanged(0, 0, 0, 0);
    // M46: ref-counted busy cursor. Each setDirectory() adds exactly one ref;
    // the scan completion/abort (or the destructor drain) removes exactly one.
    // The refcount is UI-side, so a queued job cleared by the pool can never
    // leave the app-global override cursor stuck.
    if (m_busyCursorRefs->load(std::memory_order_acquire) == 0)
        QApplication::setOverrideCursor(Qt::BusyCursor);
    m_busyCursorRefs->fetch_add(1, std::memory_order_acq_rel);

    // Snapshot the criteria the worker needs so it never reads volatile members.
    const QString typeFilter = m_typeFilter;
    const SortMode sortMode = m_sortMode;
    const bool sortAscending = m_sortAscending;
    auto alive = m_alive;
    auto genToken = m_scanGenToken;
    auto busyRefs = m_busyCursorRefs;
    const QPointer<ThumbnailPanel> self(this);

    // M24: bounded pool + QPointer marshal. The worker never touches `this`
    // directly; the completion lambda runs on the UI thread and re-checks the
    // object is still alive. The busy cursor is restored unconditionally (it
    // is app-global and ref-counted) so a destroyed/superseded scan can never
    // leave the whole application with a stuck override cursor.
    startDirectoryScan(path, gen, typeFilter, sortMode, sortAscending, m_scanProgressive, alive,
                       genToken, busyRefs, self);
    scheduleHighLatencyProbe(path, gen);
    armScanWatchdog(gen);
}

void ThumbnailPanel::startDirectoryScan(const QString &path, int gen, const QString &typeFilter,
                                        SortMode sortMode, bool sortAscending, bool progressive,
                                        const std::shared_ptr<std::atomic<bool>> &alive,
                                        const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                                        const std::shared_ptr<std::atomic<int>> &busyRefs,
                                        const QPointer<ThumbnailPanel> &self)
{
    // Captured now: a newer setDirectory() replaces the panel's ticket.
    const auto cursorReleased = m_scanCursorReleased;
    (void)QtConcurrent::run(
        &m_scanPool,
        [self, alive, gen, genToken, busyRefs, cursorReleased, path, typeFilter, sortMode,
         sortAscending, progressive]()
        {
            QList<Entry> entries;
            bool publish = false;
            bool directoryReadable = true;
            try
            {
                if (path.isEmpty())
                {
                    publish = true;
                }
                else
                {
                    const auto probe = ThumbnailPanel::scanIterationProbeSnapshot();
                    if (!scanStillCurrent(alive, genToken, gen))
                    {
                        publish = false;
                    }
                    else
                    {
                        invokeScanPreExistsProbe();
                    }
                    if (!scanStillCurrent(alive, genToken, gen))
                    {
                        publish = false;
                    }
                    else if (scanDirectoryMissing(path))
                    {
                        publish = true;
                        directoryReadable = false;
                    }
                    else if (progressive)
                    {
                        const bool aborted = scanProgressiveDirectory(
                            path, alive, genToken, gen, typeFilter.toLower(), entries, probe,
                            [self, alive, gen](const QList<Entry> &batch)
                            {
                                if (!qApp)
                                    return;
                                QMetaObject::invokeMethod(qApp,
                                                          [self, alive, gen, batch]()
                                                          {
                                                              if (alive->load() && self &&
                                                                  self->m_dirGen == gen)
                                                                  self->applyScanBatch(gen, batch);
                                                          });
                            });
                        publish = !aborted;
                    }
                    else
                    {
                        const QFileInfoList list =
                            sortedEntries(QDir(path), sortMode, sortAscending);
                        const uint64_t generation = static_cast<uint64_t>(gen);
                        for (const QFileInfo &fi : list)
                        {
                            if (!alive->load() ||
                                genToken->load(std::memory_order_acquire) != generation)
                                break;
                            callProbe(probe);
                            if (fi.suffix().isEmpty())
                                continue;
                            entries.append({fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0,
                                            fi.lastModified()});
                        }
                        publish = alive->load() &&
                                  genToken->load(std::memory_order_acquire) == generation;
                    }
                }
            }
            catch (...)
            {
                entries.clear();
                publish = true;
                directoryReadable = false;
            }
            if (!qApp)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [self, alive, gen, busyRefs, cursorReleased, entries, publish, directoryReadable]()
                {
                    releaseScanCursor(busyRefs, cursorReleased);
                    if (!alive->load() || !self || !publish)
                        return;
                    self->applyScanResult(gen, entries, directoryReadable);
                });
        });
}

// Coalesced publication of the browse sequence. See the member comment: the
// per-batch emission was quadratic over a large directory.
void ThumbnailPanel::scheduleSequencePublish()
{
    constexpr qint64 kMinIntervalMs = 100;
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    const qint64 elapsed = now - m_sequenceLastPublishMs;
    if (m_sequencePublishPending)
        return;
    if (elapsed >= kMinIntervalMs)
    {
        m_sequenceLastPublishMs = now;
        emit sequenceChanged(m_currentDir, m_paths);
        return;
    }
    m_sequencePublishPending = true;
    QTimer::singleShot(static_cast<int>(kMinIntervalMs - elapsed), this,
                       [this]()
                       {
                           m_sequencePublishPending = false;
                           m_sequenceLastPublishMs = QDateTime::currentMSecsSinceEpoch();
                           emit sequenceChanged(m_currentDir, m_paths);
                       });
}

void ThumbnailPanel::applyScanBatch(int gen, const QList<Entry> &scanned)
{
    if (gen != m_dirGen || scanned.isEmpty())
        return;
    noteScanProgress(gen);
    QList<Entry> batch = scanned;
    mviewer::browseformats::eraseHiddenEntries(batch, m_hiddenFormatGroups);
    if (batch.isEmpty())
        return;

    const int sourceRow = m_allEntries.size();
    m_allEntries.append(batch);
    for (int i = 0; i < batch.size(); ++i)
    {
        const Entry &e = batch.at(i);
        m_sourceRowByPath.insert(galleryPathKey(e.path), sourceRow + i);
        ThumbnailProvider::hintSourceIdentity(
            e.path, e.date.isValid() ? e.date.toMSecsSinceEpoch() : 0, e.size);
    }
    if (!m_scanProgressive)
        return;

    QList<Entry> visible = batch;
    if (!m_typeFilter.isEmpty())
    {
        const QString typeFilter = m_typeFilter.toLower();
        visible.erase(std::remove_if(visible.begin(), visible.end(), [&](const Entry &e)
                                     { return !entryMatchesTypeFilter(e.path, typeFilter); }),
                      visible.end());
    }
    if (visible.isEmpty())
        return;

    QList<QPair<int, QStringList>> runs;
    insertProgressiveRows(visible, &runs);
    publishIncrementalSources(m_consumerId, runs);
    // Navigation reads this list. Publish it with the rows, not only when the
    // sorted rebuild lands at the end of the scan — but coalesced: one
    // publication per event-loop turn, and never more often than every
    // 100 ms, so the consumers' O(list) work stays linear over the scan
    // instead of quadratic.
    scheduleSequencePublish();
    // Drop the busy cursor with the first rows, not when the rest of the
    // directory finishes. releaseScanCursor is once-per-scan.
    releaseScanCursor(m_busyCursorRefs, m_scanCursorReleased);
    emit statsChanged(m_paths.size(), m_totalBytes, 0, 0);
    if (!m_galleryModelReadyEmitted)
    {
        m_galleryModelReadyEmitted = true;
        emit galleryModelReady();
    }

    // Coalesce viewport demand to one event-loop turn per group of scanner
    // batches. This keeps the first screen current without posting one timer
    // and one scheduler resubmission for every 128 files.
    if (!m_scanRangeUpdatePending)
    {
        m_scanRangeUpdatePending = true;
        QTimer::singleShot(0, this,
                           [this]
                           {
                               m_scanRangeUpdatePending = false;
                               updateVisibleRange();
                           });
    }
}

// Publish a completed scan's entries on the UI thread. Runs inside the
// qApp-marshaled completion lambda (never on the scan worker); the generation
// guard makes a superseded scan a no-op.
void ThumbnailPanel::applyScanResult(int gen, const QList<Entry> &entries, bool directoryReadable)
{
    if (gen != m_dirGen) // a newer folder superseded this scan
        return;
    ++m_scanWatchSerial;
    m_scanComplete = true;
    m_scanStallAnnounced = false;
    adoptListing(entries);
    m_metaIndex.clear();
    // Seed disk-cache identity from the scan so thumbnail get()/put() reuse
    // listing mtime/size instead of re-statting every source on network paths.
    for (const Entry &e : m_allEntries)
        ThumbnailProvider::hintSourceIdentity(
            e.path, e.date.isValid() ? e.date.toMSecsSinceEpoch() : 0, e.size);
    // Progressive batches used replaceSources for convergence; completed scan
    // and subsequent filter rebuilds must use updateSources so in-flight
    // viewport decodes are retained across the rebuild.
    // Displayed rows can be a type-filtered subset and may have arrived in
    // middle-insert order. Compare against the complete query, not raw size.
    mviewer::ui::GalleryFilterSnapshot snapshot;
    snapshot.query.type = m_typeFilter.toLower().toStdString();
    snapshot.query.sort = static_cast<mviewer::core::BrowseSortField>(m_sortMode);
    snapshot.query.ascending = m_sortAscending;
    const QList<Entry> expected = mviewer::ui::evaluateGalleryFilter(m_allEntries, snapshot);
    bool sameOrder = m_scanProgressive && expected.size() == m_paths.size();
    for (int i = 0; sameOrder && i < expected.size(); ++i)
        sameOrder = expected.at(i).path == m_paths.at(i);
    const bool wasProgressive = sameOrder;
    m_scanProgressive = false;
    if (wasProgressive)
    {
        pruneThumbnailState();
        ThumbnailPipeline::instance().updateSourcesForConsumer(m_consumerId, toStdPaths(m_paths));
        emit sequenceChanged(m_currentDir, m_paths);
        emit statsChanged(m_paths.size(), m_totalBytes, selectedPaths().size(), 0);
        if (!m_pendingSelect.isEmpty() && m_rowByPath.contains(galleryPathKey(m_pendingSelect)))
        {
            selectPath(m_pendingSelect);
            m_pendingSelect.clear();
        }
    }
    else
    {
        applyFilter();
    }
    // sequenceChanged (inside the filter rebuild) publishes "Browse: N images".
    // A failure or empty result must win over that line.
    if (!directoryReadable)
        emit browseStatusChanged(tr("Failed to load directory"));
    else if (entries.isEmpty() && !m_currentDir.isEmpty())
        emit browseStatusChanged(tr("No images yet"));
    updateTypeHiddenHint();
    // Details (resolution column) and SortResolution need header probes.
    // Thumbnail/LargeIcon intentionally skip the full-directory probe so the
    // first viewport decode owns the link on high-latency folders.
    if (m_viewMode == Details || m_sortMode == SortResolution)
        ensureDimensions();
    if (!m_galleryModelReadyEmitted)
    {
        m_galleryModelReadyEmitted = true;
        emit galleryModelReady();
    }
    // Ensure viewport thumbnail demand is scheduled for the completed listing
    QTimer::singleShot(0, this, &ThumbnailPanel::updateVisibleRange);
}
