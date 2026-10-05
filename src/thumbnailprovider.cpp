#include "thumbnailprovider.h"

#include "application/ImageLoadingService.h"
#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"
#include "thumbnailcache.h"

#include "core/image/ExifOrientation.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtMetadataSemantics.h"

#include <QBuffer>
#include <QImage>
#include <QImageReader>
#include <QPainter>

namespace
{

QImage loadEmbeddedThumb(const std::vector<uint8_t> &bytes, int &ownOrientation)
{
    ownOrientation = 1;
    QBuffer buffer;
    buffer.setData(reinterpret_cast<const char *>(bytes.data()), static_cast<int>(bytes.size()));
    buffer.open(QIODevice::ReadOnly);
    QImageReader reader(&buffer, "JPEG");
    reader.setAutoTransform(false);
    ownOrientation = mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
    reader.setAutoTransform(true);
    QImage image = reader.read();
    if (!image.isNull())
        return image;
    ownOrientation = 1;
    QImage fallback;
    fallback.loadFromData(reinterpret_cast<const uchar *>(bytes.data()),
                          static_cast<int>(bytes.size()), "JPEG");
    return fallback;
}

QImage orientThumbnail(const QImage &thumb, int orientation)
{
    if (thumb.isNull() || orientation <= 1 || orientation > 8)
        return thumb;
    const QImage src = thumb.convertToFormat(QImage::Format_ARGB32);
    int displayW = 0;
    int displayH = 0;
    mviewer::core::orientedSize(src.width(), src.height(), orientation, displayW, displayH);
    if (displayW <= 0 || displayH <= 0)
        return thumb;
    QImage oriented(displayW, displayH, QImage::Format_ARGB32);
    if (oriented.isNull())
        return thumb;
    oriented.fill(Qt::black);
    for (int oy = 0; oy < displayH; ++oy)
    {
        auto *dst = reinterpret_cast<QRgb *>(oriented.scanLine(oy));
        for (int ox = 0; ox < displayW; ++ox)
        {
            int rx = 0;
            int ry = 0;
            mviewer::core::displayToRaw(ox, oy, src.width(), src.height(), orientation, rx, ry);
            if (rx < 0 || ry < 0 || rx >= src.width() || ry >= src.height())
                continue;
            const auto *row = reinterpret_cast<const QRgb *>(src.constScanLine(ry));
            dst[ox] = row[rx];
        }
    }
    return oriented;
}

QImage maybeOrientEmbedded(const QString &sourcePath, const QImage &thumb, int thumbOwnOrientation)
{
    if (thumb.isNull() || thumbOwnOrientation != 1)
        return thumb;
    QImageReader reader(sourcePath);
    reader.setAutoTransform(false);
    const QSize raw = reader.size();
    if (!raw.isValid() || raw.width() <= 0 || raw.height() <= 0)
        return thumb;
    const int orientation =
        mviewer::core::qtmetadata::orientationFromTransform(reader.transformation());
    if (!mviewer::core::embeddedThumbFollowsRawAspect(orientation, raw.width(), raw.height(),
                                                      thumb.width(), thumb.height()))
        return thumb;
    return orientThumbnail(thumb, orientation);
}

} // namespace

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
    if (scaled.width() == size && scaled.height() == size)
    {
        return (scaled.format() == QImage::Format_ARGB32 || scaled.format() == QImage::Format_RGB32)
                   ? scaled
                   : scaled.convertToFormat(QImage::Format_ARGB32);
    }

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
        int thumbOrientation = 1;
        QImage thumb = loadEmbeddedThumb(exifThumb, thumbOrientation);
        if (!thumb.isNull())
        {
            if (thumb.width() >= 64 && thumb.height() >= 64)
            {
                // Header-only size + transformation. Apply the main IFD
                // orientation only when the thumb itself is unrotated and its
                // aspect still matches the sensor (orientations 5-8).
                thumb = maybeOrientEmbedded(qp, thumb, thumbOrientation);
                mviewer::domain::ImageMetadata meta;
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
