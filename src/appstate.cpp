#include "appstate.h"

#include "runtime_storage.h"

#include <QJsonArray>
#include <QSaveFile>
#include <algorithm>

namespace
{

QString canonicalPath(QString path)
{
    if (path.isEmpty())
        return path;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(path);
}

QString pathIdentity(const QString &path)
{
    const QString cleaned = canonicalPath(path);
#ifdef Q_OS_WIN
    return cleaned.toCaseFolded();
#else
    return cleaned;
#endif
}

bool samePath(const QString &left, const QString &right)
{
    return pathIdentity(left) == pathIdentity(right);
}

QString configPath()
{
    // Per-user, non-roaming config dir (e.g. %AppData%/mviewer on Windows).
    return mviewer::runtime::filePath(QStandardPaths::AppConfigLocation,
                                      QStringLiteral("mviewer.json"));
}

} // namespace

AppState AppState::load()
{
    AppState s;
    const QString path = configPath();
    if (path.isEmpty())
        return s;

    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return s; // missing file => defaults
    // Persisted UI state is a small JSON object; never read an oversized
    // (possibly replaced) file into memory.
    constexpr qint64 kMaxStateFileBytes = 16LL * 1024 * 1024;
    if (f.size() > kMaxStateFileBytes)
        return s;

    QJsonParseError err;
    // Bounded by the real size: QIODevice::read(maxSize) sizes its result to
    // maxSize before reading.
    const QJsonDocument doc =
        QJsonDocument::fromJson(f.read(std::min<qint64>(f.size(), kMaxStateFileBytes)), &err);
    if (err.error != QJsonParseError::NoError || !doc.isObject())
        return s; // corrupt => defaults (safe)

    const QJsonObject o = doc.object();
    const QJsonArray favs = o.value("favorites").toArray();
    for (const auto &v : favs)
        if (v.isString())
            s.favorites.append(v.toString());

    const QJsonArray recent = o.value("recentFolders").toArray();
    for (const auto &v : recent)
        if (v.isString())
            s.recentFolders.append(v.toString());

    const QJsonArray hist = o.value("history").toArray();
    for (const auto &v : hist)
        if (v.isString())
            s.history.append(v.toString());

    s.lastDir = o.value("lastDir").toString();
    s.lastImage = o.value("lastImage").toString();
    s.lastThumbScroll = o.value("lastThumbScroll").toInt(0);

    // P1-3: restore Analysis workspace + nav sidebar state (defaults safe).
    s.analysisVisible = o.value("analysisVisible").toBool(false);
    s.analysisPage = o.value("analysisPage").toInt(0);

    // P1-3: restore the navigation history stack (browser back/forward).
    const QJsonArray nav = o.value("navHistory").toArray();
    for (const auto &v : nav)
        if (v.isString())
            s.navHistory.append(v.toString());
    s.navHistoryIndex = o.value("navHistoryIndex").toInt(-1);
    return s;
}

bool AppState::save() const
{
    const QString path = configPath();
    if (path.isEmpty())
        return false;

    QJsonObject o;
    QJsonArray favs;
    for (const auto &fav : favorites)
        favs.append(fav);
    o["favorites"] = favs;

    QJsonArray recent;
    for (const auto &r : recentFolders)
        recent.append(r);
    o["recentFolders"] = recent;

    QJsonArray hist;
    for (const auto &h : history)
        hist.append(h);
    o["history"] = hist;

    o["lastDir"] = lastDir;
    o["lastImage"] = lastImage;
    o["lastThumbScroll"] = lastThumbScroll;

    // P1-3: persist Analysis workspace + nav sidebar for 100% session restore.
    o["analysisVisible"] = analysisVisible;
    o["analysisPage"] = analysisPage;

    // P1-3: persist the navigation history stack so History panel + back/forward work.
    QJsonArray nav;
    for (const auto &h : navHistory)
        nav.append(h);
    o["navHistory"] = nav;
    o["navHistoryIndex"] = navHistoryIndex;

    QSaveFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    if (f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) < 0)
        return false;
    return f.commit();
}

void AppState::addFavorite(const QString &dir)
{
    const QString cleaned = canonicalPath(dir);
    if (cleaned.isEmpty() || isFavorite(cleaned))
        return;
    favorites.append(cleaned);
}

bool AppState::removeFavorite(const QString &dir)
{
    const int before = favorites.size();
    QStringList kept;
    kept.reserve(favorites.size());
    for (const QString &item : favorites)
        if (!samePath(item, dir))
            kept.append(item);
    favorites = kept;
    return favorites.size() != before;
}

bool AppState::isFavorite(const QString &dir) const
{
    for (const QString &item : favorites)
        if (samePath(item, dir))
            return true;
    return false;
}

void AppState::addRecentFolder(const QString &dir)
{
    const QString cleaned = canonicalPath(dir);
    if (cleaned.isEmpty())
        return;
    QStringList kept;
    kept.reserve(recentFolders.size());
    for (const QString &item : recentFolders)
        if (!samePath(item, cleaned))
            kept.append(item);
    kept.prepend(cleaned);
    while (kept.size() > 15)
        kept.removeLast();
    recentFolders = kept;
}

void AppState::clearRecentFolders()
{
    recentFolders.clear();
}

void AppState::addHistory(const QString &imagePath)
{
    const QString cleaned = canonicalPath(imagePath);
    if (cleaned.isEmpty())
        return;
    QStringList kept;
    kept.reserve(history.size());
    for (const QString &item : history)
        if (!samePath(item, cleaned))
            kept.append(item);
    kept.prepend(cleaned);
    while (kept.size() > 50)
        kept.removeLast();
    history = kept;
}

void AppState::clearHistory()
{
    history.clear();
}
