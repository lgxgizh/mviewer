#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// M19: single source of truth for the active directory and directory-level
// product state (favorites / recent folders).
//
// Before this class, the current directory was mirrored independently by
// MainWindow::m_currentDir, DirectoryTree::m_currentPath and
// ThumbnailPanel::m_currentDir. Widgets now listen to this model instead of
// owning their own copy of "where am I browsing".
//
// Intentionally tiny: holds state and emits change signals. Persistence still
// lives in AppState; this model is the in-session SSOT that AppState feeds and
// that widgets observe.
class DirectoryModel : public QObject
{
    Q_OBJECT
  public:
    explicit DirectoryModel(QObject *parent = nullptr);

    QString currentDirectory() const
    {
        return m_current;
    }
    QStringList favorites() const
    {
        return m_favorites;
    }
    QStringList recentFolders() const
    {
        return m_recent;
    }
    bool isEmpty() const
    {
        return m_current.isEmpty();
    }

  public slots:
    void setCurrentDirectory(const QString &path);
    void setFavorites(const QStringList &dirs);
    void setRecentFolders(const QStringList &dirs);
    void addFavorite(const QString &dir);
    void removeFavorite(const QString &dir);
    void addRecentFolder(const QString &dir);
    void clear();

  signals:
    void currentDirectoryChanged(const QString &path);
    void favoritesChanged(const QStringList &dirs);
    void recentFoldersChanged(const QStringList &dirs);

  private:
    QString m_current;
    QStringList m_favorites;
    QStringList m_recent;
};
