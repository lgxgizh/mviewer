#include "core/image/MetadataReader.h"

#include <QFileInfo>
#include <QImageReader>

namespace mviewer::core
{

std::string MetadataReader::key(const std::string& filePath)
{
    const QFileInfo fi(QString::fromStdString(filePath));
    const QString k = QString::fromStdString(filePath) + QString::number(fi.size()) +
                      QString::number(fi.lastModified().toSecsSinceEpoch());
    return k.toStdString();
}

mviewer::domain::ImageMetadata MetadataReader::read(const std::string& filePath)
{
    mviewer::domain::ImageMetadata meta;
    const QFileInfo fi(QString::fromStdString(filePath));
    if (!fi.exists())
        return meta;
    meta.filePath = filePath;
    meta.fileName = fi.fileName().toStdString();
    meta.fileSize = fi.size();
    meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();
    QImageReader reader(QString::fromStdString(filePath));
    const QSize s = reader.size();
    meta.width = s.width();
    meta.height = s.height();
    meta.hash = filePath + "|" + std::to_string(meta.fileSize) + "|" +
                std::to_string(meta.modifiedEpochSec);
    return meta;
}

} // namespace mviewer::core
