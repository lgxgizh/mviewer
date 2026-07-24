#include "directorymodel.h"

#include <QDir>

DirectoryModel::DirectoryModel(QObject *parent) : QObject(parent)
{
}

void DirectoryModel::setCurrentDirectory(const QString &path)
{
    const QString cleaned = path.isEmpty() ? QString() : QDir::cleanPath(path);
    if (m_current == cleaned)
        return;
    m_current = cleaned;
    emit currentDirectoryChanged(m_current);
}

void DirectoryModel::setFavorites(const QStringList &dirs)
{
    if (m_favorites == dirs)
        return;
    m_favorites = dirs;
    emit favoritesChanged(m_favorites);
}

void DirectoryModel::setRecentFolders(const QStringList &dirs)
{
    if (m_recent == dirs)
        return;
    m_recent = dirs;
    emit recentFoldersChanged(m_recent);
}

void DirectoryModel::addFavorite(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString cleaned = QDir::cleanPath(dir);
    if (m_favorites.contains(cleaned))
        return;
    m_favorites.append(cleaned);
    emit favoritesChanged(m_favorites);
}

void DirectoryModel::removeFavorite(const QString &dir)
{
    const QString cleaned = QDir::cleanPath(dir);
    if (!m_favorites.removeOne(cleaned))
        return;
    emit favoritesChanged(m_favorites);
}

void DirectoryModel::addRecentFolder(const QString &dir)
{
    if (dir.isEmpty())
        return;
    const QString cleaned = QDir::cleanPath(dir);
    m_recent.removeAll(cleaned);
    m_recent.prepend(cleaned);
    while (m_recent.size() > 15)
        m_recent.removeLast();
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
