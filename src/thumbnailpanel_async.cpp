// M46: ThumbnailPanel async-scan TU (ADR-014 responsibility split). The
// directory-scan worker, the dimension-probe worker, the UI-side busy-cursor
// refcount and the deterministic scan-iteration probe live here so the core
// thumbnailpanel.cpp stays under the 800-line guard while the cooperative
// supersession/cancellation machinery stays cohesive.
#include "thumbnailpanel_p.h"

#include <QtConcurrent/QtConcurrent>

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

void scanProgressiveDirectory(const QString &path,
                              const std::shared_ptr<std::atomic<bool>> &alive,
                              const std::shared_ptr<std::atomic<uint64_t>> &genToken, int gen,
                              QList<ThumbnailPanel::Entry> &entries,
                              const std::shared_ptr<const std::function<void()>> &probe,
                              const std::function<void(const QList<ThumbnailPanel::Entry> &)> &publishBatch,
                              const std::function<void()> &onAbort)
{
    QList<ThumbnailPanel::Entry> batch;
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
        if (suffix.isEmpty() ||
            !mviewer::core::ImageFormats::isSupportedSuffix(suffix.toStdString()))
            continue;
        const ThumbnailPanel::Entry entry{
            fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0, fi.lastModified()};
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

void ThumbnailPanel::setDirectory(const QString &path)
{
    m_currentDir = path;
    m_filterText.clear();
    m_filterRecursive = false;

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

void ThumbnailPanel::startDirectoryScan(
    const QString &path, int gen, const QString &typeFilter, SortMode sortMode,
    bool sortAscending, const std::shared_ptr<std::atomic<bool>> &alive,
    const std::shared_ptr<std::atomic<uint64_t>> &genToken,
    const std::shared_ptr<std::atomic<int>> &busyRefs, const QPointer<ThumbnailPanel> &self)
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
                const bool progressive = sortMode == SortName && sortAscending &&
                                         typeFilter.isEmpty();
                if (progressive)
                {
                    scanProgressiveDirectory(
                        path, alive, genToken, gen, entries, probe,
                        [self, alive, gen](const QList<Entry> &batch)
                        {
                            QMetaObject::invokeMethod(
                                qApp,
                                [self, alive, gen, batch]()
                                {
                                    if (alive->load() && self && self->m_dirGen == gen)
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
                        if (!passesTypeFilter(typeFilter, fi.suffix()) ||
                            fi.suffix().isEmpty())
                            continue;
                        entries.append(
                            {fi.absoluteFilePath(), fi.fileName(), fi.size(), 0, 0, fi.lastModified()});
                    }
                }
            }
            QMetaObject::invokeMethod(
                qApp,
                [self, alive, gen, busyRefs, entries]() mutable
                {
                    // Always drop the busy cursor, even if superseded/destroyed.
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
        m_sourceRowByPath.insert(batch.at(i).path, sourceRow + i);
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
    applyFilter();
    // Only pay the header-read cost when the Details view actually shows the
    // resolution column.
    if (m_viewMode == Details)
        ensureDimensions();
    else if (m_viewMode == Thumbnail || m_viewMode == LargeIcon)
    {
        // Keep the first thumbnail burst ahead of metadata reads. The
        // generation guard also makes a delayed callback harmless when the
        // user changes folders.
        const int dimensionGen = m_dirGen;
        QTimer::singleShot(350, this,
                           [this, dimensionGen]
                           {
                               if (dimensionGen != m_dirGen)
                                   return;
                               if (m_viewMode == Thumbnail || m_viewMode == LargeIcon)
                                   ensureDimensions();
                           });
    }
}

// ---- M46: cooperative dimension probe ---------------------------------------
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

    auto alive = m_alive;
    auto genToken = m_scanGenToken;
    const QPointer<ThumbnailPanel> self(this);
    (void)QtConcurrent::run(
        &m_scanPool,
        [self, alive, gen, genToken, paths]()
        {
            const auto probe = ThumbnailPanel::scanIterationProbeSnapshot();
            QVector<QSize> sizes;
            sizes.reserve(paths.size());
            for (int i = 0; i < paths.size(); ++i)
            {
                // M46: cooperative stop — the panel died or the directory
                // generation was superseded. Header probing is the most
                // expensive per-file background step (a 10k-folder Details
                // view would otherwise keep probing every superseded file).
                if (!alive->load() ||
                    genToken->load(std::memory_order_acquire) != static_cast<uint64_t>(gen))
                    return; // panel destroyed / folder superseded - abort fast
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
                QImageReader reader(paths.at(i));
                reader.setAutoTransform(true);
                sizes.append(reader.size());
            }
            QMetaObject::invokeMethod(
                qApp,
                [self, alive, gen, sizes]()
                {
                    if (!alive->load() || !self)
                        return;
                    ThumbnailPanel *panel = self.data();
                    if (gen != panel->m_dirGen) // folder changed while resolving
                        return;
                    for (int i = 0; i < sizes.size() && i < panel->m_allEntries.size(); ++i)
                    {
                        panel->m_allEntries[i].width = sizes[i].width();
                        panel->m_allEntries[i].height = sizes[i].height();
                    }
                    panel->viewport()->update();
                });
        });
}
