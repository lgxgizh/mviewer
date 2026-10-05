#include "directorymodel.h"

#include <QDir>

namespace
{
QString canonicalFolder(QString path)
{
    if (path.isEmpty())
        return path;
    path.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return QDir::cleanPath(path);
}

QString folderKey(const QString &path)
{
    const QString cleaned = canonicalFolder(path);
#ifdef Q_OS_WIN
    return cleaned.toCaseFolded();
#else
    return cleaned;
#endif
}

bool sameFolder(const QString &left, const QString &right)
{
    return folderKey(left) == folderKey(right);
}

QStringList uniqueFolders(const QStringList &dirs)
{
    QStringList cleaned;
    cleaned.reserve(dirs.size());
    for (const QString &dir : dirs)
    {
        const QString stored = canonicalFolder(dir);
        if (stored.isEmpty())
            continue;
        bool seen = false;
        for (const QString &have : cleaned)
            seen = seen || sameFolder(have, stored);
        if (!seen)
            cleaned.append(stored);
    }
    return cleaned;
}
} // namespace

DirectoryModel::DirectoryModel(QObject *parent) : QObject(parent)
{
}

void DirectoryModel::setCurrentDirectory(const QString &path)
{
    const QString cleaned = canonicalFolder(path);
    if (sameFolder(m_current, cleaned))
        return;
    m_current = cleaned;
    emit currentDirectoryChanged(m_current);
}

void DirectoryModel::setFavorites(const QStringList &dirs)
{
    const QStringList cleaned = uniqueFolders(dirs);
    if (m_favorites == cleaned)
        return;
    m_favorites = cleaned;
    emit favoritesChanged(m_favorites);
}

void DirectoryModel::setRecentFolders(const QStringList &dirs)
{
    const QStringList cleaned = uniqueFolders(dirs);
    if (m_recent == cleaned)
        return;
    m_recent = cleaned;
    emit recentFoldersChanged(m_recent);
}

void DirectoryModel::addFavorite(const QString &dir)
{
    const QString cleaned = canonicalFolder(dir);
    if (cleaned.isEmpty() || hasFavorite(cleaned))
        return;
    m_favorites.append(cleaned);
    emit favoritesChanged(m_favorites);
}

void DirectoryModel::removeFavorite(const QString &dir)
{
    const int before = m_favorites.size();
    QStringList kept;
    kept.reserve(m_favorites.size());
    for (const QString &item : m_favorites)
        if (!sameFolder(item, dir))
            kept.append(item);
    if (kept.size() == before)
        return;
    m_favorites = kept;
    emit favoritesChanged(m_favorites);
}

bool DirectoryModel::hasFavorite(const QString &dir) const
{
    for (const QString &item : m_favorites)
        if (sameFolder(item, dir))
            return true;
    return false;
}

void DirectoryModel::addRecentFolder(const QString &dir)
{
    const QString cleaned = canonicalFolder(dir);
    if (cleaned.isEmpty())
        return;
    QStringList kept;
    kept.reserve(m_recent.size());
    for (const QString &item : m_recent)
        if (!sameFolder(item, cleaned))
            kept.append(item);
    kept.prepend(cleaned);
    while (kept.size() > 15)
        kept.removeLast();
    if (m_recent == kept)
        return;
    m_recent = kept;
    emit recentFoldersChanged(m_recent);
}

void DirectoryModel::clear()
{
    const bool hadCurrent = !m_current.isEmpty();
    const bool hadFav = !m_favorites.isEmpty();
    const bool hadRecent = !m_recent.isEmpty();
    m_current.clear();
    m_favorites.clear();
    m_recent.clear();
    if (hadCurrent)
        emit currentDirectoryChanged(m_current);
    if (hadFav)
        emit favoritesChanged(m_favorites);
    if (hadRecent)
        emit recentFoldersChanged(m_recent);
}
