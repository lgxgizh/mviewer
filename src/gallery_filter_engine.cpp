#include "gallery_filter_engine.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageFormats.h"
#include "naturalnameorder.h"

#include <QDir>

#include <algorithm>

namespace
{

QStringView nameExtension(const QString &name)
{
    const int dot = name.lastIndexOf(QLatin1Char('.'));
    return dot >= 0 ? QStringView(name).mid(dot + 1) : QStringView();
}

} // namespace

namespace mviewer::ui
{

QString galleryPathKey(const QString &path)
{
    if (path.isEmpty())
        return path;
    QString normalized = path;
    normalized.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(normalized);
}

bool galleryEntryMatches(const GalleryEntry &entry, const GalleryFilterSnapshot &snapshot)
{
    const QString pathKey = galleryPathKey(entry.path);
    const std::string ep = entry.path.toStdString();
    if (snapshot.query.recentOnly &&
        snapshot.recents.find(mviewer::core::normalizeKey(ep)) == snapshot.recents.end())
        return false;
    if (!snapshot.query.camera.empty() && !snapshot.metaCamera.value(pathKey).toLower().contains(
                                              QString::fromStdString(snapshot.query.camera)))
        return false;
    if (!snapshot.query.lens.empty() && !snapshot.metaLens.value(pathKey).toLower().contains(
                                            QString::fromStdString(snapshot.query.lens)))
        return false;
    if (snapshot.query.iso > 0 && snapshot.metaIso.value(pathKey, -1) != snapshot.query.iso)
        return false;
    if (snapshot.text.isEmpty())
        return true;
    if (snapshot.query.metadata)
        return snapshot.metaIndex.value(pathKey).contains(snapshot.text);
    if (snapshot.useFuzzy)
        return snapshot.fuzzy.match(entry.name).hasMatch();
    return entry.name.toLower().contains(snapshot.text);
}

QList<GalleryEntry> evaluateGalleryFilter(const QList<GalleryEntry> &source,
                                          const GalleryFilterSnapshot &snapshot)
{
    QList<GalleryEntry> out;
    out.reserve(source.size());
    const auto passesType = [&](const QString &path)
    {
        if (snapshot.query.type.empty())
            return true;
        const int dot = path.lastIndexOf('.');
        if (dot < 0 || dot + 1 >= path.length())
            return false;
        const QByteArray suffix = path.mid(dot + 1).toUtf8();
        return mviewer::core::suffixMatchesTypeFilter(
            std::string_view(suffix.constData(), static_cast<size_t>(suffix.size())),
            snapshot.query.type);
    };
    for (const GalleryEntry &entry : source)
    {
        if (passesType(entry.path) && galleryEntryMatches(entry, snapshot))
            out.append(entry);
    }
    const auto less = [&snapshot](const GalleryEntry &a, const GalleryEntry &b)
    {
        const int primary =
            browsePrimaryCompare(snapshot.query.sort, a, b, snapshot.metaCamera, snapshot.metaLens);
        return browseOrderedLess(primary, a.name, b.name, a.path, b.path, snapshot.query.ascending);
    };
    std::stable_sort(out.begin(), out.end(), less);
    return out;
}

} // namespace mviewer::ui

bool browseOrderedLess(int primary, QStringView nameA, QStringView nameB, const QString &pathA,
                       const QString &pathB, bool ascending)
{
    if (primary != 0)
        return ascending ? primary < 0 : primary > 0;
    const int byName = compareNaturalName(nameA, nameB);
    if (byName != 0)
        return byName < 0;
    return QString::compare(mviewer::ui::galleryPathKey(pathA), mviewer::ui::galleryPathKey(pathB),
                            Qt::CaseSensitive) < 0;
}

int browsePrimaryCompare(mviewer::core::BrowseSortField field, const mviewer::ui::GalleryEntry &a,
                         const mviewer::ui::GalleryEntry &b,
                         const QHash<QString, QString> &metaCamera,
                         const QHash<QString, QString> &metaLens)
{
    switch (field)
    {
    case mviewer::core::BrowseSortField::Name:
        return compareNaturalName(a.name, b.name);
    case mviewer::core::BrowseSortField::Date:
        if (a.date == b.date)
            return 0;
        return a.date < b.date ? -1 : 1;
    case mviewer::core::BrowseSortField::Size:
        if (a.size == b.size)
            return 0;
        return a.size < b.size ? -1 : 1;
    case mviewer::core::BrowseSortField::Resolution:
    {
        const qint64 left = static_cast<qint64>(a.width) * a.height;
        const qint64 right = static_cast<qint64>(b.width) * b.height;
        if (left == right)
            return 0;
        return left < right ? -1 : 1;
    }
    case mviewer::core::BrowseSortField::Type:
        return nameExtension(a.name).compare(nameExtension(b.name), Qt::CaseInsensitive);
    case mviewer::core::BrowseSortField::Rating:
        return 0;
    case mviewer::core::BrowseSortField::Camera:
        return QString::compare(metaCamera.value(mviewer::ui::galleryPathKey(a.path)),
                                metaCamera.value(mviewer::ui::galleryPathKey(b.path)),
                                Qt::CaseInsensitive);
    case mviewer::core::BrowseSortField::Lens:
        return QString::compare(metaLens.value(mviewer::ui::galleryPathKey(a.path)),
                                metaLens.value(mviewer::ui::galleryPathKey(b.path)),
                                Qt::CaseInsensitive);
    }
    return 0;
}
