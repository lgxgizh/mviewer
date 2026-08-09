#include "thumbnailprovider.h"

#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"
#include "thumbnailcache.h"

#include <QImage>
#include <QPainter>

QImage ThumbnailProvider::squareFitImage(const QImage &q, int size)
{
    if (q.isNull() || size <= 0)
        return {};
    QImage pm(size, size, QImage::Format_ARGB32);
    pm.fill(Qt::transparent);
    const QImage scaled =
        q.scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&pm);
    painter.drawImage((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    painter.end();
    return pm;
}

ImageData ThumbnailProvider::produce(const std::string &path, int size)
{
    const QString qp = QString::fromStdString(path);
    QImage cached;
    if (ThumbnailCache::instance().get(qp, size, cached))
        return mvcore::fromQImage(cached);

    const ImageData decoded = Decoder::decodeScaled(path, size);
    if (decoded.isNull())
        return {};
    const QImage q = mvcore::toQImage(decoded);
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
