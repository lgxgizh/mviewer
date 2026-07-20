#include "core/image/MetadataReader.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QColorSpace>
#include <QtMath>

namespace mviewer::core
{

std::string MetadataReader::key(const std::string &filePath)
{
    const QFileInfo fi(QString::fromStdString(filePath));
    const QString k = QString::fromStdString(filePath) + QString::number(fi.size()) +
                      QString::number(fi.lastModified().toSecsSinceEpoch());
    return k.toStdString();
}

mviewer::domain::ImageMetadata MetadataReader::read(const std::string &filePath)
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

    // ── M18: richer metadata (no extra deps; QImageReader exposes what we need).
    const QString fmt = reader.format();
    meta.format = fmt.toUpper().toStdString();
    // Number of color channels (3 = RGB, 4 = RGBA, 1 = grayscale).
    const QImage::Format imgFmt = reader.imageFormat();
    switch (imgFmt)
    {
    case QImage::Format_Mono:
    case QImage::Format_MonoLSB:
    case QImage::Format_Grayscale8:
    case QImage::Format_Grayscale16:
        meta.channels = 1;
        break;
    case QImage::Format_RGB32:
    case QImage::Format_RGB16:
    case QImage::Format_RGB666:
    case QImage::Format_RGB555:
    case QImage::Format_RGBX8888:
    case QImage::Format_BGR888:
        meta.channels = 3;
        break;
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA32FPx4:
        meta.channels = 4;
        break;
    default:
        meta.channels = 0; // unknown / not yet decoded
        break;
    }
    meta.bitDepth = reader.imageFormat() == QImage::Format_Invalid
                        ? 0
                        : QImage(1, 1, reader.imageFormat()).depth();
    // EXIF orientation (1-8); QImageReader maps the raw tag to a Qt enum.
    const QImageIOHandler::Transformations xf = reader.transformation();
    int orient = 1;
    if (xf & QImageIOHandler::TransformationRotate180)
        orient = 3;
    else if (xf & QImageIOHandler::TransformationRotate90)
        orient = 6;
    else if (xf & QImageIOHandler::TransformationMirror)
        orient = 2;
    else if (xf & QImageIOHandler::TransformationFlip)
        orient = 4;
    meta.orientation = orient;

    // DPI + embedded ICC profile require the decoded image's metadata. Read at
    // a 1x1 scaled size so we get the headers/metadata cheaply without decoding
    // full pixels (important for 100MP originals).
    {
        QImageReader dpiReader(QString::fromStdString(filePath));
        dpiReader.setScaledSize(QSize(1, 1));
        QImage img;
        if (dpiReader.read(&img))
        {
            const int dpmx = img.dotsPerMeterX();
            const int dpmy = img.dotsPerMeterY();
            if (dpmx > 0)
                meta.dpiX = qRound(dpmx * 0.0254);
            if (dpmy > 0)
                meta.dpiY = qRound(dpmy * 0.0254);
            meta.hasIccProfile = !img.colorSpace().iccProfile().isEmpty();
        }
    }

    // Embedded text keys (EXIF/XMP/IPTCCore where the plugin exposes them).
    const QStringList keys = reader.textKeys();
    for (const QString &k : keys)
    {
        const QString v = reader.text(k);
        if (!v.isEmpty())
            meta.textKeys[k.toStdString()] = v.toStdString();
    }
    // Best-effort color space label from the embedded profile if present.
    if (!reader.text("Description").isEmpty())
        meta.colorSpace = reader.text("Description").toStdString();

    meta.hash = filePath + "|" + std::to_string(meta.fileSize) + "|" +
                std::to_string(meta.modifiedEpochSec);
    return meta;
}

} // namespace mviewer::core
