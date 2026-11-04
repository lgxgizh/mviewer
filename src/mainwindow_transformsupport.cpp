// Rotate/flip format helpers. Kept out of mainwindow_selection.cpp so the
// enablement path stays small and the suffix check has a single owner.
#include "mainwindow_p.h"

#include "core/image/ImageFileRotate.h"

#include <QFileInfo>
#include <QString>
#include <QStringList>

namespace
{

bool isRotatableFilePath(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QString suffix = QFileInfo(path).suffix();
    return mviewer::core::isWritableRotateFormat(suffix.toStdString());
}

} // namespace

bool anyRotatableFile(const QStringList &paths)
{
    for (const QString &path : paths)
    {
        if (isRotatableFilePath(path))
            return true;
    }
    return false;
}

QStringList rotatableFiles(const QStringList &paths)
{
    QStringList out;
    out.reserve(paths.size());
    for (const QString &path : paths)
    {
        if (isRotatableFilePath(path))
            out.append(path);
    }
    return out;
}
