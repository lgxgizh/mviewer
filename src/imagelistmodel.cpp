#include "imagelistmodel.h"

#include <QDir>
#include <QSet>

ImageListModel::ImageListModel(QObject *parent) : QObject(parent)
{
}

void ImageListModel::setPaths(const QStringList &paths, const QString &directory)
{
    const QString cleaned = directory.isEmpty() ? m_directory : QDir::cleanPath(directory);
    const bool dirChanged = (cleaned != m_directory);
    const bool pathsChanged = (paths != m_paths) || m_dirty;
    if (paths != m_paths)
        m_index.invalidate();
    m_paths = paths;
    m_directory = cleaned;
    m_dirty = false;
    if (dirChanged)
        emit directoryChanged(m_directory);
    if (pathsChanged || dirChanged)
        emit this->pathsChanged(m_paths);
}

void ImageListModel::markDirty()
{
    m_dirty = true;
}

int ImageListModel::indexOf(const QString &path) const
{
    return m_index.indexOf(m_paths, path);
}

void ImageListModel::removePaths(const QStringList &paths)
{
    if (paths.isEmpty() || m_paths.isEmpty())
        return;
    QSet<QString> doomed;
    doomed.reserve(paths.size());
    for (const QString &p : paths)
        doomed.insert(m_index.key(p));
    const qsizetype before = m_paths.size();
    m_paths.removeIf([this, &doomed](const QString &p) { return doomed.contains(m_index.key(p)); });
    if (m_paths.size() != before)
    {
        m_index.invalidate();
        emit pathsChanged(m_paths);
    }
}

void ImageListModel::clear()
{
    const bool hadPaths = !m_paths.isEmpty();
    const bool hadDir = !m_directory.isEmpty();
    m_paths.clear();
    m_index.invalidate();
    m_directory.clear();
    m_dirty = true;
    if (hadDir)
        emit directoryChanged(m_directory);
    if (hadPaths)
        emit pathsChanged(m_paths);
}
