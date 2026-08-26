#include "RenameImageUseCase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

RenameImageUseCase::Result RenameImageUseCase::execute(const std::string &oldPath,
                                                       const std::string &newName)
{
    Result r;
    QString qOldPath = QString::fromUtf8(oldPath.data(), static_cast<int>(oldPath.size()));
    QString qNewName = QString::fromUtf8(newName.data(), static_cast<int>(newName.size()));

    QFile file(qOldPath);
    QFileInfo fi(qOldPath);
    QDir dir = fi.dir();
    QString newPath = dir.absoluteFilePath(qNewName);

    if (!file.rename(newPath))
    {
        r.error = "Failed to rename file";
        return r;
    }
    r.success = true;
    r.newPath = newPath.toUtf8().toStdString();
    return r;
}
