#include "core/image/ImageObject.h"

#include <QCryptographicHash>
#include <QFileInfo>

namespace {

std::string computeFileHash(const std::string& path, int64_t size, int64_t mtime) {
    const QString key = QString::fromStdString(path) +
                        QString::number(size) +
                        QString::number(mtime);
    const QByteArray raw = QCryptographicHash::hash(
        key.toUtf8(), QCryptographicHash::Sha1);
    return std::string(raw.constData(), raw.size());
}

} // namespace

ImageObject::ImageObject(const std::string& path, const ImageData& image) {
    const QFileInfo fi(QString::fromStdString(path));
    const int64_t size = fi.size();

    mviewer::domain::ImageMetadata meta;
    meta.filePath = path;
    meta.fileName = fi.fileName().toStdString();
    meta.width = image.width;
    meta.height = image.height;
    meta.fileSize = size;
    meta.modifiedEpochSec = fi.lastModified().toSecsSinceEpoch();
    meta.hash = computeFileHash(path, size, meta.modifiedEpochSec);

    m_frame = ImageFrame(meta, image);
}

double ImageObject::luminanceMean() {
    m_frame.computeHistogram();
    return m_frame.luminanceMean();
}

void ImageObject::rgbMeans(double& r, double& g, double& b) {
    m_frame.computeHistogram();
    m_frame.rgbMeans(r, g, b);
}

void ImageObject::rgbMeans(int& r, int& g, int& b) {
    double dr, dg, db;
    m_frame.computeHistogram();
    m_frame.rgbMeans(dr, dg, db);
    r = static_cast<int>(dr); g = static_cast<int>(dg); b = static_cast<int>(db);
}

void ImageObject::computeHistogram() {
    m_frame.computeHistogram();
}
