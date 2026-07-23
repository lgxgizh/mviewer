#include "appstate.h"

#include <QJsonArray>

namespace
{

QString configPath()
{
    // Per-user, non-roaming config dir (e.g. %AppData%/mviewer on Windows).
    const QString dir = QStandardPaths::writableLocation(QStandardPaths::AppConfigLocation);
    if (dir.isEmpty())
        return QString();
    QDir().mkpath(dir);
    return dir + "/mviewer.json";
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

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll(), &err);
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

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    return f.write(QJsonDocument(o).toJson(QJsonDocument::Indented)) >= 0;
}

void AppState::addFavorite(const QString &dir)
{
    if (dir.isEmpty() || favorites.contains(dir))
        return;
    favorites.append(dir);
}

bool AppState::removeFavorite(const QString &dir)
{
    return favorites.removeAll(dir) > 0;
}

bool AppState::isFavorite(const QString &dir) const
{
    return favorites.contains(dir);
}

void AppState::addRecentFolder(const QString &dir)
{
    if (dir.isEmpty())
        return;
    recentFolders.removeAll(dir);
    recentFolders.prepend(dir);
    while (recentFolders.size() > 15)
        recentFolders.removeLast();
}

void AppState::clearRecentFolders()
{
    recentFolders.clear();
}

void AppState::addHistory(const QString &imagePath)
{
    if (imagePath.isEmpty())
        return;
    history.removeAll(imagePath);
    history.prepend(imagePath);
    while (history.size() > 50)
        history.removeLast();
}

void AppState::clearHistory()
{
    history.clear();
}
