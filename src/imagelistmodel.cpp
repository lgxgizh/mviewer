#include "imagelistmodel.h"

#include <QDir>

ImageListModel::ImageListModel(QObject *parent) : QObject(parent)
{
}

void ImageListModel::setPaths(const QStringList &paths, const QString &directory)
{
    const QString cleaned = directory.isEmpty() ? m_directory : QDir::cleanPath(directory);
    const bool dirChanged = (cleaned != m_directory);
    const bool pathsChanged = (paths != m_paths) || m_dirty;
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

void ImageListModel::removePaths(const QStringList &paths)
{
    if (paths.isEmpty() || m_paths.isEmpty())
        return;
    bool changed = false;
    for (const QString &p : paths)
    {
        if (m_paths.removeAll(p) > 0)
            changed = true;
    }
    if (changed)
        emit pathsChanged(m_paths);
}

void ImageListModel::clear()
{
    const bool hadPaths = !m_paths.isEmpty();
    const bool hadDir = !m_directory.isEmpty();
    m_paths.clear();
    m_directory.clear();
    m_dirty = true;
    if (hadDir)
        emit directoryChanged(m_directory);
    if (hadPaths)
        emit pathsChanged(m_paths);
}
