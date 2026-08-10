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
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setSortAscending(bool ascending)
{
    if (m_sortAscending == ascending)
        return;
    m_sortAscending = ascending;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setTypeFilter(const QString &types)
{
    if (m_typeFilter == types)
        return;
    m_typeFilter = types;
    if (!m_currentDir.isEmpty())
        setDirectory(m_currentDir);
}

void ThumbnailPanel::setFilter(const QString &text, bool recursive)
{
    m_filterText = text;
    m_filterRecursive = recursive;
    applyFilter();
}

void ThumbnailPanel::setMetaSearch(bool on)
{
    m_metaSearch = on;
    if (on)
        ensureMetaIndex();
    applyFilter();
}

void ThumbnailPanel::setRatingFilter(int stars)
{
    m_ratingFilter = qBound(0, stars, 5);
    applyFilter();
}

void ThumbnailPanel::setCameraFilter(const QString &camera)
{
    if (m_cameraFilter == camera)
        return;
    m_cameraFilter = camera.trimmed();
    if (!m_currentDir.isEmpty())
        applyFilter();
}

void ThumbnailPanel::setLensFilter(const QString &lens)
{
    if (m_lensFilter == lens)
        return;
    m_lensFilter = lens.trimmed();
    if (!m_currentDir.isEmpty())
        applyFilter();
}

void ThumbnailPanel::setIsoFilter(int iso)
{
    if (m_isoFilter == iso)
        return;
    m_isoFilter = iso;
    if (!m_currentDir.isEmpty())
        applyFilter();
}

void ThumbnailPanel::setTagFilter(const QString &tag)
{
    const QString t = tag.trimmed();
    if (m_tagFilter == t)
        return;
    m_tagFilter = t;
    if (!m_currentDir.isEmpty())
        applyFilter();
}

void ThumbnailPanel::setLabelFilter(int label)
{
    m_labelFilter = qBound(0, label, 6);
    applyFilter();
}

void ThumbnailPanel::setRejectFilter(bool on)
{
    m_rejectFilter = on;
    applyFilter();
}

void ThumbnailPanel::setPickFilter(bool on)
{
    m_pickFilter = on;
    applyFilter();
}

void ThumbnailPanel::setRecentFilter(bool on)
{
    m_recentFilter = on;
    applyFilter();
}

void ThumbnailPanel::clearFlagFilters()
{
    m_labelFilter = 0;
    m_rejectFilter = false;
    m_pickFilter = false;
    m_recentFilter = false;
    applyFilter();
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
    const uint64_t requestId = mviewer::core::MetadataIndexer::instance().index(
        paths,
        [alive, self, gen](const mviewer::core::MetadataIndexer::Entry &e)
        {
            if (!alive->load() || !self)
                return;
            QMetaObject::invokeMethod(
                qApp,
                [alive, self, gen, e]()
                {
                    if (!alive->load() || !self)
                        return;
                    ThumbnailPanel *panel = self.data();
                    if (gen != panel->m_dirGen)
                        return; // superseded directory: drop
                    const QString p = QString::fromStdString(e.path);
                    panel->m_metaIndex.insert(p, QString::fromStdString(e.searchBlob));
                    panel->m_metaIso.insert(p, e.iso);
                    panel->m_metaCamera.insert(p, QString::fromStdString(e.camera).trimmed());
                    panel->m_metaLens.insert(p, QString::fromStdString(e.lens).trimmed());
                });
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
    const QString t = m_filterText.trimmed().toLower();
    const bool needMeta = (m_metaSearch && !t.isEmpty()) || !m_cameraFilter.isEmpty() ||
                          !m_lensFilter.isEmpty() || m_isoFilter > 0;
    if (needMeta)
    {
        ensureMetaIndex();
        // Progressively apply as entries land: if the index is still filling,
        // wait for its completion callback to re-run applyFilter.
        if (m_metaIndexing && m_metaIndex.size() < static_cast<int>(m_allEntries.size()))
            return;
    }

    QList<Entry> src = m_allEntries;

    // Optional recursive subfolder scan (filename search only). Runs off the
    // UI thread (M25) — a recursive walk of a deep tree used to block the
    // main thread on every keystroke. State machine:
    //   * hits current for this text  -> merge and fall through to the loop
    //   * scan in flight             -> wait (completion re-runs applyFilter)
    //   * otherwise                  -> launch the scan for the current text
    // A completed scan must NOT relaunch itself (the completion callback
    // re-enters applyFilter with m_recursiveHits current).
    if (m_filterRecursive && !t.isEmpty() && !m_metaSearch && !m_currentDir.isEmpty())
    {
        if (m_recursiveHitsFor == t && !m_recursiveSearching)
        {
            src.append(m_recursiveHits);
        }
        else if (m_recursiveSearching)
        {
            return; // scan in flight: its completion re-runs this filter
        }
        else
        {
            const QString currentDir = m_currentDir;
            auto alive = m_alive;
            const QPointer<ThumbnailPanel> self(this);
            const int gen = m_dirGen;
            m_recursiveSearching = true;
            m_recursiveHitsFor.clear(); // not current until the scan completes
            (void)QtConcurrent::run(
                &m_scanPool,
                [self, alive, gen, currentDir, t]() mutable
                {
                    QList<Entry> found;
                    QDirIterator it(currentDir, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                                    QDirIterator::Subdirectories);
                    while (it.hasNext())
                    {
                        if (!alive->load())
                            return;
                        it.next();
                        const QFileInfo fi = it.fileInfo();
                        const QString suffix = fi.suffix().toLower();
                        if (suffix.isEmpty() ||
                            !mviewer::core::ImageFormats::isSupportedSuffix(suffix.toStdString()))
                            continue;
                        if (!fi.fileName().toLower().contains(t))
                            continue;
                        const QString sub = QDir(currentDir).relativeFilePath(fi.absolutePath());
                        found.append(
                            {fi.absoluteFilePath(), fi.fileName() + " [" + sub + "]", fi.size()});
                    }
                    QMetaObject::invokeMethod(
                        qApp,
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
                            panel->applyFilter(); // merge the hits and re-filter
                        });
                });
            return; // completion callback rebuilds the model
        }
    }
    else
    {
        m_recursiveSearching = false;
        m_recursiveHitsFor.clear();
        m_recursiveHits.clear();
    }

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

    QList<Entry> out;
    for (const Entry &e : src)
    {
        if (m_ratingFilter > 0 &&
            mviewer::core::RatingStore::instance().rating(e.path.toStdString()) < m_ratingFilter)
            continue;
        const std::string ep = e.path.toStdString();
        auto &rs = mviewer::core::RatingStore::instance();
        if (m_labelFilter > 0 && rs.colorLabel(ep) != m_labelFilter)
            continue;
        if (m_rejectFilter && !rs.rejected(ep))
            continue;
        if (m_pickFilter && !rs.picked(ep))
            continue;
        if (m_recentFilter)
        {
            bool inRecents = false;
            for (const auto &r : rs.recents())
                if (r == ep)
                {
                    inRecents = true;
                    break;
                }
            if (!inRecents)
                continue;
        }
        // M25: Camera / Lens filters match THEIR OWN fields only — never the
        // concatenated search blob (a camera filter must not hit a lens-only
        // string and vice versa).
        if (!m_cameraFilter.isEmpty() &&
            !m_metaCamera.value(e.path).toLower().contains(m_cameraFilter.toLower()))
            continue;
        if (!m_lensFilter.isEmpty() &&
            !m_metaLens.value(e.path).toLower().contains(m_lensFilter.toLower()))
            continue;
        if (m_isoFilter > 0 && m_metaIso.value(e.path, -1) != m_isoFilter)
            continue;
        if (!m_tagFilter.isEmpty() && !mviewer::core::TagStore::instance().hasTag(
                                          e.path.toStdString(), m_tagFilter.toStdString()))
            continue;
        if (!t.isEmpty())
        {
            if (m_metaSearch)
            {
                if (!m_metaIndex.value(e.path).contains(t))
                    continue;
            }
            else if (useFuzzy)
            {
                if (!fuzzyRe.match(e.name).hasMatch())
                    continue;
            }
            else if (!e.name.toLower().contains(t))
                continue;
        }
        out.append(e);
    }
    buildModel(out);
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
