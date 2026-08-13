#include "core/image/decoder/QtDecoder.h"

#include "core/image/ImageBuffer.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <cmath>
#include <cstring>

namespace
{

ImageData toImageData(const QImage &src)
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
        const uchar *s = img.constScanLine(y);
        uint8_t *d = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        std::memcpy(d, s, rowBytes);
    }
    return out;
}

// Populate the M6 metadata fields from a decoded QImage + its reader. Any
// field that cannot be determined is left at its default. Never throws.
void fillMetadata(const QImageReader &reader, const QImage &img,
                  mviewer::domain::ImageMetadata &meta)
{
    meta.width = img.width();
    meta.height = img.height();

    // channels + bitDepth from the source image format (before RGB888 convert).
    switch (img.format())
    {
    case QImage::Format_Grayscale8:
    case QImage::Format_Grayscale16:
        meta.channels = 1;
        break;
    case QImage::Format_RGB32:
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBX8888:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
        meta.channels = (img.hasAlphaChannel() && img.format() != QImage::Format_RGB32 &&
                         img.format() != QImage::Format_RGBX8888)
                            ? 4
                            : 3;
        break;
    case QImage::Format_RGB16:
    case QImage::Format_RGB555:
    case QImage::Format_RGB888:
    case QImage::Format_BGR888:
        meta.channels = 3;
        break;
    default:
        meta.channels = img.hasAlphaChannel() ? 4 : 3;
        break;
    }
    meta.bitDepth = img.depth() / (meta.channels > 0 ? meta.channels : 1);
    if (meta.bitDepth <= 0)
        meta.bitDepth = img.depth();

    // EXIF orientation (1-8). Qt exposes a transformation bitmask; map the
    // common masks to their EXIF orientation constant when determinable.
    const QImageIOHandler::Transformations t = reader.transformation();
    if (t == QImageIOHandler::TransformationNone)
        meta.orientation = 1;
    else if (t == QImageIOHandler::TransformationRotate90)
        meta.orientation = 6;
    else if (t == QImageIOHandler::TransformationRotate180)
        meta.orientation = 3;
    else if (t == QImageIOHandler::TransformationRotate270)
        meta.orientation = 8;
    else if (t == QImageIOHandler::TransformationMirror)
        meta.orientation = 2;
    else if (t == QImageIOHandler::TransformationMirrorAndRotate90)
        meta.orientation = 5;
    else if (t == QImageIOHandler::TransformationFlipAndRotate90)
        meta.orientation = 7;
    else if (t == (QImageIOHandler::TransformationMirror | QImageIOHandler::TransformationFlip))
        meta.orientation = 4;
    else
        meta.orientation = 1;

    // Color space (if the decoded image carries one).
    const QColorSpace cs = img.colorSpace();
    if (cs.isValid())
    {
        meta.hasIccProfile = !cs.iccProfile().isEmpty();
        const QByteArray profile = cs.iccProfile();
        const QByteArray encoded = profile.toBase64();
        meta.textKeys["MViewer.DisplayICC.Base64"] =
            std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
        switch (cs.primaries())
        {
        case QColorSpace::Primaries::SRgb:
            meta.colorSpace = "sRGB";
            break;
        case QColorSpace::Primaries::AdobeRgb:
            meta.colorSpace = "AdobeRGB";
            break;
        case QColorSpace::Primaries::DciP3D65:
            meta.colorSpace = "DisplayP3";
            break;
        default:
            meta.colorSpace = "unknown";
            break;
        }
    }
    else
    {
        meta.colorSpace = "unknown";
    }

    // Container format: prefer the reader's reported format; fall back to the
    // file extension (QImageReader leaves format() empty for some formats, e.g.
    // BMP, which are detected by content rather than a named plugin).
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
}

// F2 (M22): claim every format Qt can actually decode, derived from
// QImageReader::supportedImageFormats(). This keeps WebP/GIF (and HEIF/AVIF when
// the platform ships the plugins) first-class with full M6 metadata, without
// touching the frozen DecoderRegistry. The historical 6 are guaranteed present.
const std::vector<std::string> &supportedExts()
{
    static const std::vector<std::string> exts = []()
    {
        std::vector<std::string> out;
        const auto fmts = QImageReader::supportedImageFormats();
        out.reserve(fmts.size() + 6);
        for (const QByteArray &f : fmts)
            out.push_back(QString::fromLatin1(f).toLower().toStdString());
        for (const char *b : {"jpg", "jpeg", "bmp", "png", "tif", "tiff"})
            out.push_back(b);
        return out;
    }();
    return exts;
}

} // namespace

bool QtDecoder::canDecode(const std::string &path) const
{
    QString ext = QFileInfo(QString::fromStdString(path)).suffix().toLower();
    // Canonical aliases so .jpg/.jpeg and .tif/.tiff both match regardless of
    // which name QImageReader reports.
    if (ext == "jpg")
        ext = "jpeg";
    else if (ext == "tif")
        ext = "tiff";
    const std::string e = ext.toStdString();
    for (const auto &x : supportedExts())
        if (x == e)
            return true;
    return false;
}

ImageData QtDecoder::decodeFull(const std::string &path) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeFull(path, meta);
}

ImageData QtDecoder::decodeFull(const std::string &path,
                                mviewer::domain::ImageMetadata &outMeta) const
{
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true); // 尊重 EXIF 方向
    const QImage img = reader.read();
    if (img.isNull())
    {
        qWarning("QtDecoder: failed to decode \"%s\": %s", path.c_str(),
                 qPrintable(reader.errorString()));
        return ImageData();
    }

    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    fillMetadata(reader, img, outMeta);
    return toImageData(img);
}

ImageData QtDecoder::decodeScaled(const std::string &path, int maxEdge) const
{
    mviewer::domain::ImageMetadata meta;
    return decodeScaled(path, maxEdge, meta);
}

ImageData QtDecoder::decodeScaled(const std::string &path, int maxEdge,
                                  mviewer::domain::ImageMetadata &outMeta) const
{
    QImageReader reader(QString::fromStdString(path));
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty())
        return ImageData();
    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    outMeta.fileSize = QFileInfo(QString::fromStdString(path)).size();
    if (full.width() > maxEdge || full.height() > maxEdge)
    {
        const double ratio = static_cast<double>(maxEdge) /
                             std::max(full.width(), full.height());
        reader.setScaledSize(QSize(static_cast<int>(full.width() * ratio),
                                   static_cast<int>(full.height() * ratio)));
    }
    const QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    const int sourceWidth = full.width();
    const int sourceHeight = full.height();
    fillMetadata(reader, img, outMeta);
    // QImageReader::read() returns the scaled image, so restore the source
    // dimensions for presentation metadata. EXIF auto-transform rotates the
    // displayed geometry and must be reflected in the reported aspect.
    outMeta.width = sourceWidth;
    outMeta.height = sourceHeight;
    const auto transform = reader.transformation();
    if (transform == QImageIOHandler::TransformationRotate90 ||
        transform == QImageIOHandler::TransformationRotate270 ||
        transform == QImageIOHandler::TransformationMirrorAndRotate90 ||
        transform == QImageIOHandler::TransformationFlipAndRotate90)
        std::swap(outMeta.width, outMeta.height);
    return toImageData(img);
}

std::vector<std::string> QtDecoder::extensions() const
{
    return supportedExts();
}
