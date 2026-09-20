// M46: ThumbnailPanel async-scan TU (ADR-014 responsibility split). The
// directory-scan worker, the dimension-probe worker, the UI-side busy-cursor
// refcount and the deterministic scan-iteration probe live here so the core
// thumbnailpanel.cpp stays under the 800-line guard while the cooperative
// supersession/cancellation machinery stays cohesive.
#include "thumbnailcache.h"
#include "thumbnailpanel_p.h"

#include <QtConcurrent/QtConcurrent>

#include <QStorageInfo>

#if defined(Q_OS_WIN)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif

#include <QSet>
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
    if (typeFilter.isEmpty())
        return true;
    const QString suffix = suffixRaw.toLower();
    static const QStringList rawExts = {"cr2", "cr3", "nef", "arw", "dng", "raf", "rw2",
                                        "orf", "sr2", "srw", "pef", "3fr", "mef", "erf",
                                        "mrw", "dcr", "kdc", "mos", "raw", "iiq"};
    for (const QString &ext : typeFilter.split(','))
    {
        const QString lowered = ext.trimmed().toLower();
        if (lowered == suffix)
            return true;
        // P0: Expand "raw" alias to common RAW file extensions.
        if (lowered == "raw" && rawExts.contains(suffix))
            return true;
        // P0: Expand "tiff" alias to "tif" + "tiff".
        if (lowered == "tiff" && (suffix == "tif" || suffix == "tiff"))
            return true;
    }
    return false;
}

void scanProgressiveDirectory(
    const QString &path, const std::shared_ptr<std::atomic<bool>> &alive,
    const std::shared_ptr<std::atomic<uint64_t>> &genToken, int gen,
    QList<ThumbnailPanel::Entry> &entries,
    const std::shared_ptr<const std::function<void()>> &probe,
    const std::function<void(const QList<ThumbnailPanel::Entry> &)> &publishBatch,
    const std::function<void()> &onAbort)
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
        {
            onAbort();
            return;
        }
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
        batch.append(entry);
        if (batch.size() >= 128)
        {
            publishBatch(batch);
            batch.clear();
        }
    }
    if (!batch.isEmpty())
        publishBatch(batch);
    std::sort(entries.begin(), entries.end(),
              [](const ThumbnailPanel::Entry &a, const ThumbnailPanel::Entry &b)
              { return QString::compare(a.path, b.path, Qt::CaseSensitive) < 0; });
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
    m_highLatencyDir = isHighLatencyBrowsePath(path);
    ThumbnailCache::instance().clearSourceIdentityHints();

    // P0-1 (perf): a new directory generation. Any in-flight background
    // dimension resolve or directory scan from the previous folder is
    // invalidated by the bump. M46: the shared generation token additionally
    // makes in-flight workers STOP cooperatively (they re-check it every
    // iteration), instead of running to completion and being discarded later.
    ++m_dirGen;
    const int gen = m_dirGen;
    m_scanGenToken->store(static_cast<uint64_t>(gen), std::memory_order_release);
    m_dimsResolved = false;
    m_scanComplete = false;
    m_scanProgressive = m_sortMode == SortName && m_sortAscending && m_typeFilter.isEmpty();

    // M23 P2 (first-screen): paint the (empty) directory shell immediately so a
    // 1000-image folder shows its grid in well under 1s, then scan the disk off
    // the UI thread and stream the real entries in once they are ready.
    resetDirectoryState();
    m_model->setStringList({});
    // Supersede thumbnail demand at the same T0 as the directory transition;
    // discovered batches below then append into an empty, current source set.
    ThumbnailPipeline::instance().setSources({});
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
    startDirectoryScan(path, gen, typeFilter, sortMode, sortAscending, alive, genToken, busyRefs,
                       self);
}

void ThumbnailPanel::startDirectoryScan(const QString &path, int gen, const QString &typeFilter,
                                        SortMode sortMode, bool sortAscending,
                                        const std::shared_ptr<std::atomic<bool>> &alive,
                                        const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                                        const std::shared_ptr<std::atomic<int>> &busyRefs,
                                        const QPointer<ThumbnailPanel> &self)
{
    (void)QtConcurrent::run(
        &m_scanPool,
        [self, alive, gen, genToken, busyRefs, path, typeFilter, sortMode, sortAscending]()
        {
            QList<Entry> entries;
            QDir dir(path);
            const auto probe = ThumbnailPanel::scanIterationProbeSnapshot();
            if (dir.exists())
            {
                const bool progressive =
                    sortMode == SortName && sortAscending && typeFilter.isEmpty();
                if (progressive)
                {
                    scanProgressiveDirectory(
                        path, alive, genToken, gen, entries, probe,
                        [self, alive, gen](const QList<Entry> &batch)
                        {
                            QMetaObject::invokeMethod(qApp,
                                                      [self, alive, gen, batch]()
                                                      {
                                                          if (alive->load() && self &&
                                                              self->m_dirGen == gen)
                                                              self->applyScanBatch(gen, batch);
                                                      });
                        },
                        [busyRefs] { ThumbnailPanel::marshalBusyRestore(busyRefs); });
                }
                else
                {
                    const QFileInfoList list = sortedEntries(dir, sortMode, sortAscending);
                    for (int i = 0; i < list.size(); ++i)
                    {
                        // M46: cooperative stop — the panel died OR a newer
                        // directory superseded this generation (A → B → C while
                        // walking A). The completion below drops this scan, but
                        // aborting here bounds the wasted work.
                        if (!alive->load() ||
                            genToken->load(std::memory_order_acquire) != static_cast<uint64_t>(gen))
                        {
                            marshalBusyRestore(busyRefs);
                            return;
                        }
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
                        const QFileInfo &fi = list.at(i);
                        if (fi.suffix().isEmpty())
                            continue;
                        entries.append({fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0,
                                        fi.lastModified()});
                    }
                }
            }
            QMetaObject::invokeMethod(qApp,
                                      [self, alive, gen, busyRefs, entries]() mutable
                                      {
                                          // Always drop the busy cursor, even if
                                          // superseded/destroyed.
                                          restoreBusyCursorOnce(busyRefs);
                                          if (!alive->load() || !self)
                                              return;
                                          self.data()->applyScanResult(gen, entries);
                                      });
        });
}

void ThumbnailPanel::applyScanBatch(int gen, const QList<Entry> &batch)
{
    if (gen != m_dirGen || batch.isEmpty())
        return;

    const int sourceRow = m_allEntries.size();
    m_allEntries.append(batch);
    for (int i = 0; i < batch.size(); ++i)
    {
        const Entry &e = batch.at(i);
        m_sourceRowByPath.insert(e.path, sourceRow + i);
        ThumbnailCache::instance().hintSourceIdentity(
            e.path, e.date.isValid() ? e.date.toMSecsSinceEpoch() : 0, e.size);
    }
    if (!m_scanProgressive)
        return;

    const int firstRow = m_paths.size();
    if (!m_model->insertRows(firstRow, batch.size()))
        return;
    m_displayEntries.append(batch);
    m_displayEntryRow.reserve(m_displayEntries.size());
    for (int i = 0; i < batch.size(); ++i)
    {
        const Entry &entry = batch.at(i);
        const int row = firstRow + i;
        m_paths.append(entry.path);
        m_rowByPath.insert(entry.path, row);
        m_displayEntryRow.insert(entry.path, row);
        m_sizeByPath.insert(entry.path, entry.size);
        m_model->setData(m_model->index(row, 0), entry.name);
        m_totalBytes += entry.size;
    }
    QStringList paths;
    paths.reserve(batch.size());
    for (const Entry &entry : batch)
        paths.append(entry.path);
    ThumbnailPipeline::instance().appendSources(toStdPaths(paths));
    emit statsChanged(m_paths.size(), m_totalBytes, 0, 0);

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
void ThumbnailPanel::applyScanResult(int gen, const QList<Entry> &entries)
{
    if (gen != m_dirGen) // a newer folder superseded this scan
        return;
    m_scanComplete = true;
    m_allEntries = entries;
    m_sourceRowByPath.clear();
    m_sourceRowByPath.reserve(m_allEntries.size());
    for (int i = 0; i < m_allEntries.size(); ++i)
        m_sourceRowByPath.insert(m_allEntries.at(i).path, i);
    m_metaIndex.clear();
    // Seed disk-cache identity from the scan so thumbnail get()/put() reuse
    // listing mtime/size instead of re-statting every source on network paths.
    for (const Entry &e : m_allEntries)
        ThumbnailCache::instance().hintSourceIdentity(
            e.path, e.date.isValid() ? e.date.toMSecsSinceEpoch() : 0, e.size);
    applyFilter();
    // Details (resolution column) and SortResolution need header probes.
    // Thumbnail/LargeIcon intentionally skip the full-directory probe so the
    // first viewport decode owns the link on high-latency folders.
    if (m_viewMode == Details || m_sortMode == SortResolution)
        ensureDimensions();
}

// ---- M46: cooperative dimension probe (viewport-first) ----------------------
namespace
{
struct DimProbeResult
{
    QSize size;
    int frameCount = 1;
    bool animated = false;
};

DimProbeResult probeEntryDimensions(const QString &path)
{
    DimProbeResult out;
    const auto sequence = mviewer::core::FrameSequenceReader::probe(path.toUtf8().toStdString());
    if (sequence.valid)
    {
        const auto frame = mviewer::core::FrameSequenceReader::frameInfo(
            path.toUtf8().toStdString(), sequence.defaultFrame);
        out.size = QSize(frame.width, frame.height);
        out.frameCount = qMax(1, sequence.frameCount);
        out.animated = sequence.animated;
    }
    if (!out.size.isValid())
    {
        QImageReader reader(path);
        reader.setAutoTransform(true);
        out.size = reader.size();
    }
    return out;
}

using DimBatchPublish = std::function<void(const QVector<int> &, const QVector<QSize> &,
                                           const QVector<int> &, const QVector<bool> &, bool)>;

void runDimensionProbeLoop(const std::shared_ptr<std::atomic<bool>> &alive, int gen,
                           const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                           const QStringList &paths, const QVector<int> &order,
                           const std::shared_ptr<const std::function<void()>> &probe,
                           const DimBatchPublish &publish)
{
    QVector<QSize> sizes(paths.size());
    QVector<int> frameCounts(paths.size(), 1);
    QVector<bool> animatedFlags(paths.size(), false);
    QVector<int> batchIdx;
    batchIdx.reserve(32);

    auto flush = [&](bool finalBatch)
    {
        if (batchIdx.isEmpty() && !finalBatch)
            return;
        QVector<QSize> sizeCopy;
        QVector<int> frameCopy;
        QVector<bool> animCopy;
        sizeCopy.reserve(batchIdx.size());
        frameCopy.reserve(batchIdx.size());
        animCopy.reserve(batchIdx.size());
        for (int i : batchIdx)
        {
            sizeCopy.append(sizes.at(i));
            frameCopy.append(frameCounts.at(i));
            animCopy.append(animatedFlags.at(i));
        }
        publish(batchIdx, sizeCopy, frameCopy, animCopy, finalBatch);
        batchIdx.clear();
    };

    for (int oi = 0; oi < order.size(); ++oi)
    {
        if (!alive->load() ||
            genToken->load(std::memory_order_acquire) != static_cast<uint64_t>(gen))
            return;
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
        const int i = order.at(oi);
        if (i < 0 || i >= paths.size())
            continue;
        const DimProbeResult probed = probeEntryDimensions(paths.at(i));
        sizes[i] = probed.size;
        frameCounts[i] = probed.frameCount;
        animatedFlags[i] = probed.animated;
        batchIdx.append(i);
        if (batchIdx.size() >= ((oi < 64) ? 16 : 48))
            flush(false);
    }
    flush(true);
}
} // namespace

QVector<int> ThumbnailPanel::dimensionProbeOrder() const
{
    QVector<int> order;
    order.reserve(m_allEntries.size());
    QSet<int> seen;
    const int visibleRows = m_paths.size();
    if (visibleRows > 0 && viewport()->height() >= 10)
    {
        int first = 0;
        int last = 0;
        if (m_viewMode == ViewMode::Details)
        {
            const int offset = qMax(0, verticalScrollBar()->value());
            const int visible = qMax(
                1, (offset % kDetailsItemHeight + viewport()->height() + kDetailsItemHeight - 1) /
                       kDetailsItemHeight);
            first = offset / kDetailsItemHeight;
            last = first + visible - 1;
        }
        else
        {
            const QSize cell = gridSize();
            const int cellW = qMax(1, cell.width());
            const int cellH = qMax(1, cell.height());
            const int cols = qMax(1, viewport()->width() / cellW);
            const int firstRow = verticalScrollBar()->value() / cellH;
            first = firstRow * cols;
            last = first + cols * qMax(1, viewport()->height() / cellH);
        }
        first = qBound(0, first, visibleRows - 1);
        last = qBound(first, last, visibleRows - 1);
        for (int row = first; row <= last; ++row)
        {
            const int src = m_sourceRowByPath.value(m_paths.at(row), -1);
            if (src >= 0 && !seen.contains(src))
            {
                order.append(src);
                seen.insert(src);
            }
        }
    }
    for (int i = 0; i < m_allEntries.size(); ++i)
    {
        if (!seen.contains(i))
            order.append(i);
    }
    return order;
}

void ThumbnailPanel::applyDimensionBatch(const QVector<int> &idx, const QVector<QSize> &sizes,
                                         const QVector<int> &frames, const QVector<bool> &animated)
{
    for (int b = 0; b < idx.size(); ++b)
    {
        const int i = idx.at(b);
        if (i < 0 || i >= m_allEntries.size())
            continue;
        m_allEntries[i].width = sizes.at(b).width();
        m_allEntries[i].height = sizes.at(b).height();
        m_allEntries[i].frameCount = frames.at(b);
        m_allEntries[i].animated = animated.at(b);
        const QString &path = m_allEntries.at(i).path;
        const int drow = m_displayEntryRow.value(path, -1);
        if (drow >= 0 && drow < m_displayEntries.size())
        {
            m_displayEntries[drow].width = sizes.at(b).width();
            m_displayEntries[drow].height = sizes.at(b).height();
            m_displayEntries[drow].frameCount = frames.at(b);
            m_displayEntries[drow].animated = animated.at(b);
        }
    }
}

void ThumbnailPanel::publishDimensionBatch(const QPointer<ThumbnailPanel> &self,
                                           const std::shared_ptr<std::atomic<bool>> &alive, int gen,
                                           const QVector<int> &idx, const QVector<QSize> &sizes,
                                           const QVector<int> &frames,
                                           const QVector<bool> &animated, bool finalBatch,
                                           bool resortWhenDone)
{
    QMetaObject::invokeMethod(
        qApp,
        [self, alive, gen, idx, sizes, frames, animated, finalBatch, resortWhenDone]()
        {
            if (!alive->load() || !self)
                return;
            ThumbnailPanel *panel = self.data();
            if (gen != panel->m_dirGen)
                return;
            panel->applyDimensionBatch(idx, sizes, frames, animated);
            if (finalBatch && resortWhenDone)
                panel->scheduleFilter(false);
            else
                panel->viewport()->update();
        });
}

void ThumbnailPanel::dimensionProbeTask(const QPointer<ThumbnailPanel> &self,
                                        const std::shared_ptr<std::atomic<bool>> &alive, int gen,
                                        const std::shared_ptr<std::atomic<uint64_t>> &genToken,
                                        const QStringList &paths, const QVector<int> &order,
                                        bool resortWhenDone)
{
    const auto probe = ThumbnailPanel::scanIterationProbeSnapshot();
    runDimensionProbeLoop(
        alive, gen, genToken, paths, order, probe,
        [self, alive, gen, resortWhenDone](const QVector<int> &idx, const QVector<QSize> &sizes,
                                           const QVector<int> &frames,
                                           const QVector<bool> &animated, bool finalBatch)
        {
            ThumbnailPanel::publishDimensionBatch(self, alive, gen, idx, sizes, frames, animated,
                                                  finalBatch, resortWhenDone);
        });
}

void ThumbnailPanel::ensureDimensions()
{
    if (m_dimsResolved || m_allEntries.isEmpty())
        return;
    m_dimsResolved = true; // mark up-front so we launch the worker only once

    const int gen = m_dirGen;
    QStringList paths;
    paths.reserve(m_allEntries.size());
    for (const Entry &e : m_allEntries)
        paths.append(e.path);
    const QVector<int> order = dimensionProbeOrder();
    auto alive = m_alive;
    auto genToken = m_scanGenToken;
    const QPointer<ThumbnailPanel> self(this);
    const bool resortWhenDone = (m_sortMode == SortResolution);
    (void)QtConcurrent::run(&m_scanPool,
                            [self, alive, gen, genToken, paths, order, resortWhenDone]()
                            {
                                ThumbnailPanel::dimensionProbeTask(self, alive, gen, genToken,
                                                                   paths, order, resortWhenDone);
                            });
}
