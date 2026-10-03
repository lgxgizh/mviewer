#include "imagelistmodel.h"

#include <QDir>

namespace
{
bool equivalentListPath(const QString &left, const QString &right)
{
    const QString normalizedLeft = QDir::cleanPath(QDir::fromNativeSeparators(left));
    const QString normalizedRight = QDir::cleanPath(QDir::fromNativeSeparators(right));
#ifdef Q_OS_WIN
    return normalizedLeft.compare(normalizedRight, Qt::CaseInsensitive) == 0;
#else
    return normalizedLeft == normalizedRight;
#endif
}
} // namespace

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

int ImageListModel::indexOf(const QString &path) const
{
    for (int i = 0; i < m_paths.size(); ++i)
    {
        if (equivalentListPath(m_paths.at(i), path))
            return i;
    }
    return -1;
}

void ImageListModel::removePaths(const QStringList &paths)
{
    if (paths.isEmpty() || m_paths.isEmpty())
        return;
    bool changed = false;
    for (int i = m_paths.size() - 1; i >= 0; --i)
    {
        for (const QString &p : paths)
        {
            if (equivalentListPath(m_paths.at(i), p))
            {
                m_paths.removeAt(i);
                changed = true;
                break;
            }
        }
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
