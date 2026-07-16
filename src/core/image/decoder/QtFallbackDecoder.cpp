#include "core/image/decoder/QtFallbackDecoder.h"

#include "core/image/ImageBuffer.h"

#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <cmath>
#include <cstring>

namespace
{

ImageData toImageData(const QImage& src)
{
    if (src.isNull())
        return ImageData();
    const QImage img = src.convertToFormat(QImage::Format_RGB888);
    if (img.isNull())
        return ImageData();
    ImageData out = makeImageData(img.width(), img.height(), PixelFormat::RGB24);
    const int w = img.width();
    const int h = img.height();
    const size_t rowBytes = static_cast<size_t>(w) * 3;
    for (int y = 0; y < h; ++y)
    {
        const uchar* s = img.constScanLine(y);
        uint8_t* d = out.buffer.get() + static_cast<size_t>(y) * out.stride();
        std::memcpy(d, s, rowBytes);
    }
    return out;
}

void fillMeta(const QImageReader& reader, const QImage& img,
              mviewer::domain::ImageMetadata& meta)
{
    meta.width = img.width();
    meta.height = img.height();
    meta.channels = img.hasAlphaChannel() ? 4 : 3;
    meta.bitDepth = img.depth() / meta.channels;
    if (meta.bitDepth <= 0)
        meta.bitDepth = img.depth();
    const QByteArray fmt = reader.format();
    if (!fmt.isEmpty())
    {
        meta.format = QString::fromLatin1(fmt).toUpper().toStdString();
    }
    else
    {
        const QString ext = QFileInfo(reader.fileName()).suffix().toLower();
        if (ext == "jpg" || ext == "jpeg")
            meta.format = "JPEG";
        else if (ext == "png")
            meta.format = "PNG";
        else if (ext == "bmp")
            meta.format = "BMP";
        else if (ext == "tif" || ext == "tiff")
            meta.format = "TIFF";
        else if (!ext.isEmpty())
            meta.format = ext.toUpper().toStdString();
    }
    meta.colorSpace = "unknown";
    meta.orientation = 1;
}

} // namespace

bool QtFallbackDecoder::canDecode(const std::string&) const
{
    // Always attempts; registered LAST so specific decoders get first pick.
    return true;
}

ImageData QtFallbackDecoder::decodeFull(const std::string& path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData QtFallbackDecoder::decodeFull(const std::string& path,
                                                 mviewer::domain::ImageMetadata& outMeta) const
{
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true);
    const QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    fillMeta(reader, img, outMeta);
    return toImageData(img);
}

ImageData QtFallbackDecoder::decodeScaled(const std::string& path, int maxEdge) const
{
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty())
        return ImageData();
    if (full.width() <= maxEdge && full.height() <= maxEdge)
        return toImageData(reader.read());
    const double ratio =
        static_cast<double>(maxEdge) / std::max(full.width(), full.height());
    reader.setScaledSize(QSize(static_cast<int>(full.width() * ratio),
                               static_cast<int>(full.height() * ratio)));
    return toImageData(reader.read());
}

std::vector<std::string> QtFallbackDecoder::extensions() const
{
    return {}; // matches everything via canDecode() == true
}
