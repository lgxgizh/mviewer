#pragma once

#include <QObject>
#include <QString>
#include <QStringList>

// M19: single source of truth for the image path list of the active directory.
//
// Before this class the same list lived in three places:
//   MainWindow::m_cachedImagePaths, ThumbnailPanel::m_paths (unfiltered source),
//   ImageViewer::m_fileList. Navigation, Compare pool seeding and status-bar
//   counts each re-scanned or re-copied independently.
//
// This model holds the *directory listing* (unfiltered). ThumbnailPanel may
// still maintain a filtered presentation list derived from filters/sort — that
// is view state, not a second source of "what images are in this folder".
//
// Intentionally tiny: holds state and emits change signals.
class ImageListModel : public QObject
{
    Q_OBJECT
  public:
    explicit ImageListModel(QObject *parent = nullptr);

    QStringList paths() const
    {
        return m_paths;
    }
    QString directory() const
    {
        return m_directory;
    }
    int count() const
    {
        return m_paths.size();
    }
    bool isEmpty() const
    {
        return m_paths.isEmpty();
    }
    bool isDirty() const
    {
        return m_dirty;
    }
    int indexOf(const QString &path) const
    {
        return m_paths.indexOf(path);
    }
    QString pathAt(int index) const
    {
        return m_paths.value(index);
    }

  public slots:
    // Replace the listing for `directory`. Clears the dirty flag.
    void setPaths(const QStringList &paths, const QString &directory = {});
    // Mark the listing stale so the next consumer reloads from disk.
    void markDirty();
    void removePaths(const QStringList &paths);
    void clear();

  signals:
    void pathsChanged(const QStringList &paths);
    void directoryChanged(const QString &directory);

  private:
    QStringList m_paths;
    QString m_directory;
    bool m_dirty = true;
};
