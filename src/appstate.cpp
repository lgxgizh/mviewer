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
