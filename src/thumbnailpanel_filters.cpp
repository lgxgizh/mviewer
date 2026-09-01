// ThumbnailPanel filtering & sorting: filters, metadata index, sorted entries
// (M20 P0#3). M25: metadata indexing moved off the UI thread into the shared
// MetadataIndexer; Camera/Lens filters are field-scoped; sort keys are
// computed once per file and compared in pure memory.
#include "thumbnailpanel_p.h"

#include "core/image/ImageSortKeys.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/search/MetadataFilter.h"

#include <QtConcurrent/QtConcurrent>

#include <algorithm>

void ThumbnailPanel::setSortMode(SortMode mode)
{
    if (m_sortMode == mode)
        return;
    m_sortMode = mode;
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setSortAscending(bool ascending)
{
    if (m_sortAscending == ascending)
        return;
    m_sortAscending = ascending;
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setTypeFilter(const QString &types)
{
    if (m_typeFilter == types)
        return;
    m_typeFilter = types;
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setFilter(const QString &text, bool recursive)
{
    m_filterText = text;
    m_filterRecursive = recursive;
    scheduleFilter(true);
    // Reflect a pending non-empty query immediately so a stale directory
    // result is never presented as the answer to the new filter. The actual
    // evaluation remains debounced and latest-wins in runFilterQuery().
    // Tiny directories can clear their stale projection immediately without
    // creating a meaningful UI slice; the actual query still runs on the
    // debounce timer. Large directories keep the previous model until the
    // guarded worker result arrives, avoiding a 50K-row synchronous reset.
    if (!m_filterText.trimmed().isEmpty() && m_allEntries.size() <= 256)
    {
        QStringList previousSelection = selectedPaths();
        QString previousCurrent =
            currentIndex().isValid() ? m_paths.value(currentIndex().row()) : QString();
        // A second keystroke can arrive before the first debounced query
        // publishes. In that case the native model is already empty; retain
        // the original identity captured by the pending query.
        if (m_pendingFilterGeneration != 0)
        {
            previousSelection = m_pendingFilterSelection;
            previousCurrent = m_pendingFilterCurrent;
        }
        buildModel({});
        m_pendingFilterSelection = previousSelection;
        m_pendingFilterCurrent = previousCurrent;
        m_pendingFilterGeneration = m_filterGeneration;
    }
}

void ThumbnailPanel::setMetaSearch(bool on)
{
    m_metaSearch = on;
    if (on)
        ensureMetaIndex();
    scheduleFilter(false);
}

void ThumbnailPanel::setRatingFilter(int stars)
{
    m_ratingFilter = qBound(0, stars, 5);
    scheduleFilter(false);
}

void ThumbnailPanel::setCameraFilter(const QString &camera)
{
    if (m_cameraFilter == camera)
        return;
    m_cameraFilter = camera.trimmed();
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setLensFilter(const QString &lens)
{
    if (m_lensFilter == lens)
        return;
    m_lensFilter = lens.trimmed();
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setIsoFilter(int iso)
{
    if (m_isoFilter == iso)
        return;
    m_isoFilter = iso;
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setTagFilter(const QString &tag)
{
    const QString t = tag.trimmed();
    if (m_tagFilter == t)
        return;
    m_tagFilter = t;
    if (!m_currentDir.isEmpty())
        scheduleFilter(false);
}

void ThumbnailPanel::setLabelFilter(int label)
{
    m_labelFilter = qBound(0, label, 6);
    scheduleFilter(false);
}

void ThumbnailPanel::setRejectFilter(bool on)
{
    m_rejectFilter = on;
    scheduleFilter(false);
}

void ThumbnailPanel::setPickFilter(bool on)
{
    m_pickFilter = on;
    scheduleFilter(false);
}

void ThumbnailPanel::setRecentFilter(bool on)
{
    m_recentFilter = on;
    scheduleFilter(false);
}

void ThumbnailPanel::clearFlagFilters()
{
    m_labelFilter = 0;
    m_rejectFilter = false;
    m_pickFilter = false;
    m_recentFilter = false;
    scheduleFilter(false);
}

void ThumbnailPanel::invalidateRatings()
{
    viewport()->update();
}

void ThumbnailPanel::ensureMetaIndex()
{
    if (m_metaIndexing || m_allEntries.isEmpty())
        return;
    // A camera/lens/ISO or metadata-text filter needs the whole-directory
    // metadata index. Build it OFF the UI thread through the shared
    // MetadataIndexer (which also serves MainWindow's search re-index), scoped
    // to the current directory generation so a folder switch cancels it.
    if (!m_metaIndex.isEmpty())
        return;

    m_metaIndexing = true;
    const int gen = m_dirGen;
    auto alive = m_alive;
    const QPointer<ThumbnailPanel> self(this);
    // Index the FULL directory listing, not the currently filtered m_paths —
    // a camera/lens filter must be able to evaluate every candidate, and a
    // filter change may re-add previously filtered-out files.
    std::vector<std::string> paths;
    paths.reserve(static_cast<size_t>(m_allEntries.size()));
    for (const Entry &e : m_allEntries)
        paths.push_back(e.path.toStdString());

    // M26: supersede ONLY this panel's own previous request — never the
    // MainWindow search re-index running concurrently on the same directory.
    if (m_metaRequestId != 0)
        mviewer::core::MetadataIndexer::instance().cancelRequest(m_metaRequestId);
    const uint64_t requestId = mviewer::core::MetadataIndexer::instance().indexBatched(
        paths,
        [alive, self, gen](const std::vector<mviewer::core::MetadataIndexer::Entry> &batch)
        {
            if (!alive->load() || !self)
                return;
            // M58: MetadataIndexer already marshals this one bounded batch to
            // the GUI thread, so update the value maps without another hop.
            ThumbnailPanel *panel = self.data();
            if (gen != panel->m_dirGen)
                return;
            for (const auto &e : batch)
            {
                const QString p = QString::fromUtf8(e.path.data(), static_cast<int>(e.path.size()));
                panel->m_metaIndex.insert(
                    p,
                    QString::fromUtf8(e.searchBlob.data(), static_cast<int>(e.searchBlob.size())));
                panel->m_metaIso.insert(p, e.iso);
                panel->m_metaCamera.insert(
                    p, QString::fromUtf8(e.camera.data(), static_cast<int>(e.camera.size()))
                           .trimmed());
                panel->m_metaLens.insert(
                    p, QString::fromUtf8(e.lens.data(), static_cast<int>(e.lens.size())).trimmed());
            }
            // M58 progressive non-default query state: a batch landing may
            // refine the visible result immediately; the generation gate
            // coalesces bursts and drops stale work.
            panel->scheduleFilter(false);
        },
        [alive, self, gen]()
        {
            if (!alive->load() || !self)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [alive, self, gen]()
                {
                    if (!alive->load() || !self)
                        return;
                    ThumbnailPanel *panel = self.data();
                    if (gen != panel->m_dirGen)
                        return;
                    panel->m_metaIndexing = false;
                    panel->applyFilter(); // index complete: re-run the pending filter
                });
        });
    // A rejected submission means no callbacks will ever arrive — do not leave
    // the panel stuck in the indexing state.
    if (requestId == 0)
    {
        m_metaIndexing = false;
        return;
    }
    m_metaRequestId = requestId;
}

void ThumbnailPanel::applyFilter()
{
    scheduleFilter(false);
}

void ThumbnailPanel::scheduleFilter(bool debounce)
{
    ++m_filterGeneration;
    if (m_filterTask)
        TaskScheduler::cancel(m_filterTask);
    if (m_filterCancel)
        m_filterCancel->store(true, std::memory_order_release);
    m_filterCancel = std::make_shared<std::atomic<bool>>(false);
    if (!m_filterDebounceTimer)
        return;
    m_filterDebounceTimer->stop();
    if (debounce)
    {
        m_filterDebounceTimer->setInterval(25);
        m_filterDebounceTimer->start();
    }
    else
    {
        // Discrete controls (sort/type/flags) submit immediately; evaluation
        // is still background and latest-wins, so no heavy O(N) work runs in
        // this UI handler and tests/users do not observe an extra timer turn.
        runFilterQuery();
    }
}

void ThumbnailPanel::runFilterQuery()
{
    if (!m_scanComplete)
        m_scanProgressive = false;
    const QString t = m_filterText.trimmed().toLower();
    const bool needMeta = (m_metaSearch && !t.isEmpty()) || !m_cameraFilter.isEmpty() ||
                          !m_lensFilter.isEmpty() || m_isoFilter > 0;
    if (needMeta)
    {
        ensureMetaIndex();
        // M58: do not block on the full index. The worker evaluates the
        // currently available snapshot and each bounded metadata batch
        // schedules a newer generation, so non-default queries converge
        // progressively instead of waiting for the last file.
    }

    QList<Entry> src;
    if (!prepareFilterSource(t, src))
        return;

    // Build a fuzzy/wildcard regex when the search text contains * or ?.
    // Otherwise fall back to the fast substring match.
    QRegularExpression fuzzyRe;
    const bool useFuzzy = !t.isEmpty() && !m_metaSearch && (t.contains('*') || t.contains('?'));
    if (useFuzzy)
    {
        // Convert glob pattern to regex: * → .*, ? → ., escape everything else.
        QString pattern = QRegularExpression::escape(t);
        pattern.replace("\\*", ".*").replace("\\?", ".");
        fuzzyRe.setPattern(pattern);
        fuzzyRe.setPatternOptions(QRegularExpression::CaseInsensitiveOption);
    }

    // Build one value query and one store snapshot at the UI boundary. The
    // worker never reads mutable widgets, singleton stores, or Qt models.
    mviewer::core::BrowseQuery query;
    query.text = t.toStdString();
    query.recursive = m_filterRecursive;
    query.metadata = m_metaSearch;
    query.ratingMinimum = m_ratingFilter;
    query.colorLabel = m_labelFilter;
    query.rejectedOnly = m_rejectFilter;
    query.pickedOnly = m_pickFilter;
    query.recentOnly = m_recentFilter;
    query.camera = m_cameraFilter.toLower().toStdString();
    query.lens = m_lensFilter.toLower().toStdString();
    query.iso = m_isoFilter;
    query.tag = m_tagFilter.toStdString();
    query.type = m_typeFilter.toLower().toStdString();
    query.sort = static_cast<mviewer::core::BrowseSortField>(m_sortMode);
    query.ascending = m_sortAscending;
    query.generation = m_filterGeneration;
    const auto ratings = mviewer::core::RatingStore::instance().snapshot();
    const auto tags = mviewer::core::TagStore::instance().snapshot();
    const auto metaIndex = m_metaIndex;
    const auto metaIso = m_metaIso;
    const auto metaCamera = m_metaCamera;
    const auto metaLens = m_metaLens;
    const bool incremental = m_incrementalApply;
    const QStringList prevSelection = m_incrementalPrevSelection;
    const QString prevCurrent = m_incrementalPrevCurrent;
    const QString anchorPath = m_incrementalAnchorPath;
    const int anchorOffset = m_incrementalAnchorOffset;
    const uint64_t generation = m_filterGeneration;
    const auto cancel = m_filterCancel;
    const auto alive = m_alive;
    const QPointer<ThumbnailPanel> self(this);
    // Tiny directories do not benefit from queueing and the synchronous path
    // keeps established selection/filter semantics deterministic. Large
    // directories always take the cancellable worker path below.
    if (src.size() <= 256)
    {
        const QList<Entry> out =
            evaluateFilterSnapshot(src, t, useFuzzy, fuzzyRe, query, ratings, tags, metaIndex,
                                   metaIso, metaCamera, metaLens);
        if (m_incrementalApply)
            applyDisplayedEntriesIncremental(out, prevSelection, prevCurrent, anchorPath,
                                             anchorOffset);
        else
            buildModel(out);
        return;
    }
    m_filterTask = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [self, alive, cancel, generation, src, t, fuzzyRe, useFuzzy, query, ratings, tags,
         metaIndex, metaIso, metaCamera, metaLens, incremental, prevSelection, prevCurrent,
         anchorPath, anchorOffset](const TaskScheduler::TaskContext &ctx) mutable
        {
            if (ctx.isCancelled() || cancel->load(std::memory_order_acquire) || !alive->load())
                return;
            const QList<Entry> out = ThumbnailPanel::evaluateFilterSnapshot(
                src, t, useFuzzy, fuzzyRe, query, ratings, tags, metaIndex, metaIso, metaCamera,
                metaLens);
            if (ctx.isCancelled() || cancel->load(std::memory_order_acquire) || !alive->load() ||
                !self)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [self, alive, cancel, generation, out, incremental, prevSelection, prevCurrent,
                 anchorPath, anchorOffset]()
                {
                    if (!alive->load() || cancel->load(std::memory_order_acquire) || !self ||
                        generation != self->m_filterGeneration)
                        return;
                    if (incremental)
                        self->applyDisplayedEntriesIncremental(out, prevSelection, prevCurrent,
                                                               anchorPath, anchorOffset);
                    else
                        self->buildModel(out);
                },
                Qt::QueuedConnection);
        });
}

QList<ThumbnailPanel::Entry> ThumbnailPanel::evaluateFilterSnapshot(
    const QList<Entry> &source, const QString &text, bool useFuzzy, const QRegularExpression &fuzzy,
    const mviewer::core::BrowseQuery &query, const mviewer::core::RatingStore::Snapshot &ratings,
    const mviewer::core::TagStore::Snapshot &tags, const QHash<QString, QString> &metaIndex,
    const QHash<QString, int> &metaIso, const QHash<QString, QString> &metaCamera,
    const QHash<QString, QString> &metaLens)
{
    QList<Entry> out;
    out.reserve(source.size());
    const auto passesType = [&query](const QString &path)
    {
        if (query.type.empty())
            return true;
        const QString suffix = QFileInfo(path).suffix().toLower();
        static const QStringList rawExts = {"cr2", "cr3", "nef", "arw", "dng", "raf", "rw2",
                                            "orf", "sr2", "srw", "pef", "3fr", "mef", "erf",
                                            "mrw", "dcr", "kdc", "mos", "raw", "iiq"};
        for (const QString &candidate :
             QString::fromStdString(query.type).split(',', Qt::SkipEmptyParts))
        {
            const QString type = candidate.trimmed().toLower();
            if (type == suffix || (type == "tiff" && (suffix == "tif" || suffix == "tiff")) ||
                (type == "raw" && rawExts.contains(suffix)))
                return true;
        }
        return false;
    };
    for (const Entry &entry : source)
    {
        if (passesType(entry.path) &&
            matchesFilterSnapshot(entry, text, useFuzzy, fuzzy, query, ratings, tags, metaIndex,
                                  metaIso, metaCamera, metaLens))
            out.append(entry);
    }
    const auto less = [&query, &ratings, &metaCamera, &metaLens](const Entry &a, const Entry &b)
    {
        int cmp = 0;
        switch (query.sort)
        {
        case mviewer::core::BrowseSortField::Name:
            cmp = QString::compare(a.name, b.name, Qt::CaseInsensitive);
            break;
        case mviewer::core::BrowseSortField::Date:
            cmp = a.date < b.date ? -1 : (a.date > b.date ? 1 : 0);
            break;
        case mviewer::core::BrowseSortField::Size:
            cmp = a.size < b.size ? -1 : (a.size > b.size ? 1 : 0);
            break;
        case mviewer::core::BrowseSortField::Resolution:
        {
            const qint64 ar = static_cast<qint64>(a.width) * a.height;
            const qint64 br = static_cast<qint64>(b.width) * b.height;
            cmp = ar < br ? -1 : (ar > br ? 1 : 0);
            break;
        }
        case mviewer::core::BrowseSortField::Type:
            cmp = QString::compare(QFileInfo(a.path).suffix(), QFileInfo(b.path).suffix(),
                                   Qt::CaseInsensitive);
            break;
        case mviewer::core::BrowseSortField::Rating:
            cmp = ratings.rating(a.path.toStdString()) - ratings.rating(b.path.toStdString());
            break;
        case mviewer::core::BrowseSortField::Camera:
            cmp = QString::compare(metaCamera.value(a.path), metaCamera.value(b.path),
                                   Qt::CaseInsensitive);
            break;
        case mviewer::core::BrowseSortField::Lens:
            cmp = QString::compare(metaLens.value(a.path), metaLens.value(b.path),
                                   Qt::CaseInsensitive);
            break;
        }
        if (cmp == 0)
            cmp = QString::compare(a.path, b.path, Qt::CaseSensitive);
        return query.ascending ? cmp < 0 : cmp > 0;
    };
    std::stable_sort(out.begin(), out.end(), less);
    return out;
}

bool ThumbnailPanel::prepareFilterSource(const QString &t, QList<Entry> &src)
{
    src = m_allEntries;
    if (m_filterRecursive && !t.isEmpty() && !m_metaSearch && !m_currentDir.isEmpty())
    {
        if (m_recursiveHitsFor == t && !m_recursiveSearching)
            src.append(m_recursiveHits);
        else if (m_recursiveSearching)
            return false;
        else
        {
            const QString currentDir = m_currentDir;
            auto alive = m_alive;
            auto genToken = m_scanGenToken;
            const QPointer<ThumbnailPanel> self(this);
            const int gen = m_dirGen;
            m_recursiveSearching = true;
            m_recursiveHitsFor.clear();
            (void)QtConcurrent::run(
                &m_scanPool,
                [self, alive, gen, genToken, currentDir, t]() mutable
                {
                    QList<Entry> found;
                    QDirIterator it(currentDir, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                                    QDirIterator::Subdirectories);
                    while (it.hasNext())
                    {
                        // M46: cooperative stop on panel death or directory
                        // supersession — a stale recursive walk must not keep
                        // enumerating the new folder's tree.
                        if (!alive->load() ||
                            genToken->load(std::memory_order_acquire) != static_cast<uint64_t>(gen))
                            return;
                        it.next();
                        const QFileInfo fi = it.fileInfo();
                        const QString suffix = fi.suffix().toLower();
                        if (suffix.isEmpty() ||
                            !mviewer::core::ImageFormats::isSupportedSuffix(suffix.toStdString()) ||
                            !fi.fileName().toLower().contains(t))
                            continue;
                        const QString sub = QDir(currentDir).relativeFilePath(fi.absolutePath());
                        // M46: the Entry carries scan-time size AND mtime so the
                        // Details delegate can paint from cache only.
                        found.append({fi.absoluteFilePath(), fi.fileName() + " [" + sub + "]",
                                      fi.size(), 0, 0, fi.lastModified()});
                    }
                    QMetaObject::invokeMethod(qApp,
                                              [self, alive, gen, found, t]()
                                              {
                                                  if (!alive->load() || !self)
                                                      return;
                                                  ThumbnailPanel *panel = self.data();
                                                  panel->m_recursiveSearching = false;
                                                  if (gen != panel->m_dirGen)
                                                      return;
                                                  panel->m_recursiveHits = found;
                                                  panel->m_recursiveHitsFor = t;
                                                  panel->applyFilter();
                                              });
                });
            return false;
        }
    }
    else
    {
        m_recursiveSearching = false;
        m_recursiveHitsFor.clear();
        m_recursiveHits.clear();
    }
    return true;
}

bool ThumbnailPanel::matchesFilter(const Entry &e, const QString &t, bool useFuzzy,
                                   const QRegularExpression &fuzzy) const
{
    mviewer::core::BrowseQuery query;
    query.text = t.toStdString();
    query.metadata = m_metaSearch;
    query.ratingMinimum = m_ratingFilter;
    query.colorLabel = m_labelFilter;
    query.rejectedOnly = m_rejectFilter;
    query.pickedOnly = m_pickFilter;
    query.recentOnly = m_recentFilter;
    query.camera = m_cameraFilter.toLower().toStdString();
    query.lens = m_lensFilter.toLower().toStdString();
    query.iso = m_isoFilter;
    query.tag = m_tagFilter.toStdString();
    return matchesFilterSnapshot(e, t, useFuzzy, fuzzy, query,
                                 mviewer::core::RatingStore::instance().snapshot(),
                                 mviewer::core::TagStore::instance().snapshot(), m_metaIndex,
                                 m_metaIso, m_metaCamera, m_metaLens);
}

bool ThumbnailPanel::matchesFilterSnapshot(
    const Entry &e, const QString &t, bool useFuzzy, const QRegularExpression &fuzzy,
    const mviewer::core::BrowseQuery &query, const mviewer::core::RatingStore::Snapshot &ratings,
    const mviewer::core::TagStore::Snapshot &tags, const QHash<QString, QString> &metaIndex,
    const QHash<QString, int> &metaIso, const QHash<QString, QString> &metaCamera,
    const QHash<QString, QString> &metaLens)
{
    const std::string ep = e.path.toStdString();
    if (query.ratingMinimum > 0 && ratings.rating(ep) < query.ratingMinimum)
        return false;
    if (query.colorLabel > 0 && ratings.colorLabel(ep) != query.colorLabel)
        return false;
    if (query.rejectedOnly && !ratings.isRejected(ep))
        return false;
    if (query.pickedOnly && !ratings.isPicked(ep))
        return false;
    if (query.recentOnly && !ratings.isRecent(ep))
        return false;
    if (!query.camera.empty() &&
        !metaCamera.value(e.path).toLower().contains(QString::fromStdString(query.camera)))
        return false;
    if (!query.lens.empty() &&
        !metaLens.value(e.path).toLower().contains(QString::fromStdString(query.lens)))
        return false;
    if (query.iso > 0 && metaIso.value(e.path, -1) != query.iso)
        return false;
    if (!query.tag.empty() && !tags.hasTag(ep, query.tag))
        return false;
    if (t.isEmpty())
        return true;
    if (query.metadata)
        return metaIndex.value(e.path).contains(t);
    if (useFuzzy)
        return fuzzy.match(e.name).hasMatch();
    return e.name.toLower().contains(t);
}

// static
QFileInfoList ThumbnailPanel::sortedEntries(const QDir &dir, SortMode mode, bool ascending)
{
    // M25: the directory scan already runs off the UI thread; it now computes
    // ONE sort key per file up front (O(N) reads, not O(N log N)) and then
    // sorts in pure memory — no file I/O inside a comparator.
    QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    QFileInfoList out;
    for (const QFileInfo &fi : files)
    {
        const QString suffix = fi.suffix().toLower();
        if (!suffix.isEmpty() &&
            mviewer::core::ImageFormats::isSupportedSuffix(suffix.toStdString()))
            out.append(fi);
    }
    if (out.size() < 2)
        return out;

    mviewer::core::SortField field = mviewer::core::SortField::Name;
    switch (mode)
    {
    case SortName:
        field = mviewer::core::SortField::Name;
        break;
    case SortDate:
        field = mviewer::core::SortField::Date;
        break;
    case SortSize:
        field = mviewer::core::SortField::Size;
        break;
    case SortResolution:
        field = mviewer::core::SortField::Resolution;
        break;
    case SortType:
        field = mviewer::core::SortField::Type;
        break;
    case SortRating:
        field = mviewer::core::SortField::Rating;
        break;
    case SortCamera:
        field = mviewer::core::SortField::Camera;
        break;
    case SortLens:
        field = mviewer::core::SortField::Lens;
        break;
    }

    std::vector<std::string> paths;
    paths.reserve(static_cast<size_t>(out.size()));
    for (const QFileInfo &fi : out)
        paths.push_back(fi.absoluteFilePath().toStdString());

    const auto keys = mviewer::core::computeSortKeys(paths, field);

    // Pure-memory sort over precomputed keys.
    std::vector<int> order(static_cast<size_t>(out.size()));
    for (size_t i = 0; i < order.size(); ++i)
        order[i] = static_cast<int>(i);
    std::sort(order.begin(), order.end(),
              [&](int a, int b)
              {
                  const auto &ka = keys[static_cast<size_t>(a)];
                  const auto &kb = keys[static_cast<size_t>(b)];
                  switch (mode)
                  {
                  case SortName:
                      return ka.path < kb.path;
                  case SortDate:
                      if (ka.mtimeSec != kb.mtimeSec)
                          return ka.mtimeSec > kb.mtimeSec;
                      return ka.path < kb.path;
                  case SortSize:
                      if (ka.size != kb.size)
                          return ka.size > kb.size;
                      return ka.path < kb.path;
                  case SortResolution:
                      if (ka.resolution != kb.resolution)
                          return ka.resolution > kb.resolution;
                      return ka.path < kb.path;
                  case SortType:
                      if (ka.suffix != kb.suffix)
                          return ka.suffix < kb.suffix;
                      return ka.path < kb.path;
                  case SortRating:
                      if (ka.rating != kb.rating)
                          return ka.rating < kb.rating;
                      return ka.path < kb.path;
                  case SortCamera:
                      if (ka.camera != kb.camera)
                          return ka.camera < kb.camera;
                      return ka.path < kb.path;
                  case SortLens:
                      if (ka.lens != kb.lens)
                          return ka.lens < kb.lens;
                      return ka.path < kb.path;
                  }
                  return ka.path < kb.path;
              });

    QFileInfoList sorted;
    sorted.reserve(out.size());
    for (int i : order)
        sorted.append(out.at(i));
    // A-2.2: apply sort direction (reverse if descending).
    if (!ascending)
        std::reverse(sorted.begin(), sorted.end());
    return sorted;
}
