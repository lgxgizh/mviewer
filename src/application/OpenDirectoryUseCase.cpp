#include "OpenDirectoryUseCase.h"
#include "core/filesystem/FileSystem.h"
#include <algorithm>

OpenDirectoryUseCase::Result OpenDirectoryUseCase::execute(const std::string &directoryPath, int maxImages)
{
    Result r;
    // Convert std::string to QString for FileSystem call
    QString qDir = QString::fromStdString(directoryPath);
    QStringList images = FileSystem::listImages(qDir, maxImages);
    for (const QString &p : images) {
        r.imagePaths.push_back(p.toStdString());
    }
    if (r.imagePaths.empty()) {
        r.error = "No supported images found in directory";
    }
    return r;
}
