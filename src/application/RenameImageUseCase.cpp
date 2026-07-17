#include "RenameImageUseCase.h"

#include <QDir>
#include <QFile>
#include <QFileInfo>

RenameImageUseCase::Result RenameImageUseCase::execute(const std::string &oldPath,
                                                       const std::string &newName)
{
    Result r;
    QString qOldPath = QString::fromStdString(oldPath);
    QString qNewName = QString::fromStdString(newName);

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
    r.newPath = newPath.toStdString();
    return r;
}
