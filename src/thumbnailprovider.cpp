#include "thumbnailprovider.h"

#include "application/ImageLoadingService.h"
#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"
#include "thumbnailcache.h"

#include "core/image/MetadataReader.h"

#include <QImage>
#include <QPainter>

QImage ThumbnailProvider::squareFitImage(const QImage &q, int size)
{
    if (q.isNull() || size <= 0)
        return {};
    if (q.width() == size && q.height() == size &&
        (q.format() == QImage::Format_ARGB32 || q.format() == QImage::Format_RGB32))
        return q;

    const bool alreadyFitted =
        (q.width() == size && q.height() <= size) || (q.height() == size && q.width() <= size);
    const QImage scaled =
        alreadyFitted ? q : q.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    if (scaled.width() == size && scaled.height() == size &&
        (scaled.format() == QImage::Format_ARGB32 || scaled.format() == QImage::Format_RGB32))
        return scaled;

    QImage pm(size, size, QImage::Format_ARGB32);
    pm.fill(Qt::transparent);
    QPainter painter(&pm);
    painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    painter.end();
    return pm;
}

ImageData ThumbnailProvider::produce(const std::string &path, int size)
{
    if (path.empty() || size <= 0)
        return {};
    const QString qp = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    QImage cached;
    if (ThumbnailCache::instance().get(qp, size, cached))
        return mvcore::fromQImage(cached);

    // Fast path: attempt embedded EXIF thumbnail extraction for JPEG/TIFF/RAW
    const std::vector<uint8_t> exifThumb =
        mviewer::core::MetadataReader::extractExifThumbnail(path);
    if (!exifThumb.empty())
    {
        QImage thumb;
        if (thumb.loadFromData(reinterpret_cast<const uchar *>(exifThumb.data()),
                               static_cast<int>(exifThumb.size()), "JPEG") &&
            !thumb.isNull())
        {
            if (thumb.width() >= 64 && thumb.height() >= 64)
            {
                mviewer::domain::ImageMetadata meta = mviewer::core::MetadataReader::read(path);
                const QImage q = mvcore::toDisplayQImage(mvcore::fromQImage(thumb), meta);
                const QImage fitted = squareFitImage(q.isNull() ? thumb : q, size);
                if (!fitted.isNull())
                {
                    ThumbnailCache::instance().put(qp, size, fitted);
                    return mvcore::fromQImage(fitted);
                }
            }
        }
    }

    mviewer::domain::ImageMetadata meta;
    const ImageData decoded = Decoder::decodeScaled(path, size, meta);
    if (decoded.isNull())
        return {};
    const QImage q = mvcore::toDisplayQImage(decoded, meta);
    if (q.isNull())
        return {};
    const QImage fitted = squareFitImage(q, size);
    if (fitted.isNull())
        return {};
    // Disk-cache store happens on the WORKER (PNG encode/write must never run
    // on the GUI thread).
    ThumbnailCache::instance().put(qp, size, fitted);
    return mvcore::fromQImage(fitted);
}

void ThumbnailProvider::invalidateSource(const std::string &path)
{
    if (path.empty())
        return;
    ThumbnailCache::instance().invalidatePath(
        QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    mviewer::application::ImageLoadingService::instance().invalidateSource(path);
}
