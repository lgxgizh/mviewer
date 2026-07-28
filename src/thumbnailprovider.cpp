#include "thumbnailprovider.h"

#include "core/image/Decoder.h"
#include "core/image/QtConvert.h"
#include "thumbnailcache.h"

#include <QImage>
#include <QPainter>

ImageData ThumbnailProvider::decode(const std::string &path, int size)
{
    QString qp = QString::fromStdString(path);
    QPixmap cached;
    if (ThumbnailCache::instance().get(qp, cached))
        return mvcore::fromQImage(cached.toImage());
    return Decoder::decodeScaled(path, size);
}

QPixmap ThumbnailProvider::squareFit(const ImageData &img, int size)
{
    if (img.isNull())
        return {};
    const QImage q = mvcore::toQImage(img);
    if (q.isNull())
        return {};
    QPixmap pm(size, size);
    pm.fill(Qt::transparent);
    QPixmap scaled =
        QPixmap::fromImage(q).scaled(size, size, Qt::KeepAspectRatio, Qt::SmoothTransformation);
    QPainter painter(&pm);
    painter.drawPixmap((size - scaled.width()) / 2, (size - scaled.height()) / 2, scaled);
    painter.end();
    return pm;
}

void ThumbnailProvider::cache(const QString &path, const QPixmap &pm)
{
    ThumbnailCache::instance().put(path, pm);
}

QPixmap ThumbnailProvider::produce(const std::string &path, const ImageData &img, int size)
{
    QPixmap pm = squareFit(img, size);
    if (pm.isNull())
        return pm;
    cache(QString::fromStdString(path), pm);
    return pm;
}
