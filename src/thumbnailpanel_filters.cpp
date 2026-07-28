// ThumbnailPanel filtering & sorting: filters, metadata index, sorted entries (M20 P0#3).
#include "thumbnailpanel_p.h"

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
    if (!m_metaIndex.isEmpty())
        return;
    for (const Entry &e : m_allEntries)
    {
        const mviewer::domain::ImageMetadata meta =
            mviewer::core::MetadataReader::read(e.path.toStdString());
        const mviewer::core::RawMetadata rm = mviewer::core::parseRawMetadata(e.path.toStdString());
        QStringList parts;
        parts << QString::fromStdString(meta.fileName) << QString::fromStdString(meta.filePath)
              << QString::fromStdString(meta.format);
        for (const auto &[k, v] : meta.textKeys)
        {
            parts << QString::fromStdString(k) << QString::fromStdString(v);
        }
        parts << QString::fromStdString(rm.make) << QString::fromStdString(rm.model)
              << QString::fromStdString(rm.lens);
        if (rm.iso > 0)
            parts << QString::number(rm.iso);
        m_metaIndex.insert(e.path, parts.join(' ').toLower());
        m_metaIso.insert(e.path, rm.iso);
    }
}

void ThumbnailPanel::applyFilter()
{
    const QString t = m_filterText.trimmed().toLower();
    if (m_metaSearch && !t.isEmpty())
        ensureMetaIndex();
    if (!m_cameraFilter.isEmpty() || !m_lensFilter.isEmpty() || m_isoFilter > 0)
        ensureMetaIndex();

    QList<Entry> src = m_allEntries;

    // Optional recursive subfolder scan (filename search only).
    if (m_filterRecursive && !t.isEmpty() && !m_metaSearch && !m_currentDir.isEmpty())
    {
        QDirIterator it(m_currentDir, QDir::Files | QDir::Readable | QDir::NoDotAndDotDot,
                        QDirIterator::Subdirectories);
        while (it.hasNext())
        {
            it.next();
            const QFileInfo fi = it.fileInfo();
            if (!isImageSuffix(fi.suffix().toLower()))
                continue;
            if (!fi.fileName().toLower().contains(t))
                continue;
            const QString sub = QDir(m_currentDir).relativeFilePath(fi.absolutePath());
            src.append({fi.absoluteFilePath(), fi.fileName() + " [" + sub + "]", fi.size()});
        }
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
        if (!m_cameraFilter.isEmpty() &&
            !m_metaIndex.value(e.path).contains(m_cameraFilter.toLower()))
            continue;
        if (!m_lensFilter.isEmpty() && !m_metaIndex.value(e.path).contains(m_lensFilter.toLower()))
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
    QStringList imgExts = {"bmp", "gif", "jpg", "jpeg", "png", "tif", "tiff", "webp", "cr2",
                           "cr3", "nef", "nrw", "arw",  "dng", "orf", "rw2",  "pef",  "raf"};
    QFileInfoList files = dir.entryInfoList(QDir::Files | QDir::Readable, QDir::NoSort);
    QFileInfoList out;
    for (const QFileInfo &fi : files)
        if (imgExts.contains(fi.suffix().toLower()))
            out.append(fi);

    switch (mode)
    {
    case SortName:
        std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0; });
        break;
    case SortDate:
        std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.lastModified() > b.lastModified(); });
        break;
    case SortSize:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b) { return a.size() > b.size(); });
        break;
    case SortResolution:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  {
                      auto readSize = [](const QString &path) -> qint64
                      {
                          QImageReader reader(path);
                          reader.setAutoTransform(true);
                          const QSize s = reader.size();
                          return static_cast<qint64>(s.width()) * s.height();
                      };
                      return readSize(a.absoluteFilePath()) > readSize(b.absoluteFilePath());
                  });
        break;
    case SortType:
        std::sort(out.begin(), out.end(), [](const QFileInfo &a, const QFileInfo &b)
                  { return a.suffix().compare(b.suffix(), Qt::CaseInsensitive) < 0; });
        break;
    case SortRating:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  {
                      const int ra = mviewer::core::RatingStore::instance().rating(
                          a.absoluteFilePath().toStdString());
                      const int rb = mviewer::core::RatingStore::instance().rating(
                          b.absoluteFilePath().toStdString());
                      if (ra != rb)
                          return ra < rb;
                      return a.fileName().compare(b.fileName(), Qt::CaseInsensitive) < 0;
                  });
        break;
    case SortCamera:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  {
                      auto cam = [](const QString &p) -> QString
                      {
                          const auto rm = mviewer::core::parseRawMetadata(p.toStdString());
                          return QString::fromStdString(rm.make + " " + rm.model).toLower();
                      };
                      return cam(a.absoluteFilePath())
                                 .compare(cam(b.absoluteFilePath()), Qt::CaseInsensitive) < 0;
                  });
        break;
    case SortLens:
        std::sort(out.begin(), out.end(),
                  [](const QFileInfo &a, const QFileInfo &b)
                  {
                      auto lens = [](const QString &p) -> QString
                      {
                          const auto rm = mviewer::core::parseRawMetadata(p.toStdString());
                          return QString::fromStdString(rm.lens).toLower();
                      };
                      return lens(a.absoluteFilePath())
                                 .compare(lens(b.absoluteFilePath()), Qt::CaseInsensitive) < 0;
                  });
        break;
    }

    // A-2.2: apply sort direction (reverse if descending).
    if (!ascending)
        std::reverse(out.begin(), out.end());
    return out;
}

// ---- ThumbDelegate ----------------------------------------------------------
