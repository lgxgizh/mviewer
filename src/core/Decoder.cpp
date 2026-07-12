#include "Decoder.h"

#include <QImageReader>

QStringList Decoder::supportedExtensions()
{
    return {"*.jpg", "*.jpeg", "*.bmp", "*.png"};
}

QImage Decoder::decodeFull(const QString &path)
{
    QImageReader reader(path);
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    return reader.read();
}

QImage Decoder::decodeScaled(const QString &path, int maxEdge)
{
    QImageReader reader(path);
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty())
        return QImage();
    if (full.width() <= maxEdge && full.height() <= maxEdge)
        return reader.read(); // 本身已够小，直接解

    const double ratio = static_cast<double>(maxEdge) /
                         std::max(full.width(), full.height());
    reader.setScaledSize(
        QSize(static_cast<int>(full.width() * ratio),
              static_cast<int>(full.height() * ratio)));
    return reader.read();
}
