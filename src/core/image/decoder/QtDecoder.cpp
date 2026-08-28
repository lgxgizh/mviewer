#include "core/image/decoder/QtDecoder.h"
#include "core/filesystem/Utf8Path.h"

#include "core/image/ImageBuffer.h"

#include <QColorSpace>
#include <QFileInfo>
#include <QImage>
#include <QImageReader>
#include <QString>
#include <algorithm>
#include <cmath>
#include <cstring>
#include <limits>
#include <vector>

#if defined(Q_OS_WIN)
#define NOMINMAX
#include <windows.h>
#include <wincodec.h>
#include <wrl/client.h>
#endif

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

// Map the QImageIOHandler transformation bitmask to the EXIF orientation 1-8
// constant (the SINGLE authoritative Qt<->EXIF mapping; every consumer of the
// orientation field depends on it — M48 B3 regression pins 4/5/7). Qt 6.10
// encodes: Mirror=1, Flip=2, Rotate180=Mirror|Flip=3, Rotate90=4,
// MirrorAndRotate90=5 (EXIF 7), FlipAndRotate90=6 (EXIF 5), Rotate270=7.
int orientationFromTransform(QImageIOHandler::Transformations t)
{
    if (t == QImageIOHandler::TransformationNone)
        return 1;
    if (t == QImageIOHandler::TransformationMirror)
        return 2;
    if (t == QImageIOHandler::TransformationRotate180)
        return 3;
    if (t == QImageIOHandler::TransformationFlip)
        return 4;
    if (t == QImageIOHandler::TransformationFlipAndRotate90)
        return 5; // EXIF transpose
    if (t == QImageIOHandler::TransformationRotate90)
        return 6;
    if (t == QImageIOHandler::TransformationMirrorAndRotate90)
        return 7; // EXIF transverse
    if (t == QImageIOHandler::TransformationRotate270)
        return 8;
    return 1;
}

bool transformSwapsDimensions(QImageIOHandler::Transformations t)
{
    return t == QImageIOHandler::TransformationRotate90 ||
           t == QImageIOHandler::TransformationRotate270 ||
           t == QImageIOHandler::TransformationMirrorAndRotate90 ||
           t == QImageIOHandler::TransformationFlipAndRotate90;
}

bool isTiffPath(const std::string &path)
{
    const QString ext = QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size())))
                            .suffix()
                            .toLower();
    return ext == "tif" || ext == "tiff";
}

void fillMetadata(const QImageReader &reader, const QImage &img,
                  mviewer::domain::ImageMetadata &meta);

#if defined(Q_OS_WIN)

// Qt's TIFF plugin reports ClipRect but still attempts to allocate the full
// source raster before honoring it. Windows Imaging Component (WIC) exposes a
// lazy TIFF frame source, so clipping/scaling happens before CopyPixels and
// only the requested display raster is materialized here.
QImage decodeTiffWic(const std::string &path, const QRect &rawRect, const QSize &target,
                     mviewer::domain::ImageMetadata &meta)
{
    if (target.width() <= 0 || target.height() <= 0 || rawRect.width() <= 0 ||
        rawRect.height() <= 0)
        return QImage();

    const QString filename = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    const std::wstring filenameWide = filename.toStdWString();

    class ComScope
    {
      public:
        ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED)) {}
        ~ComScope()
        {
            if (SUCCEEDED(result_))
                CoUninitialize();
        }
        bool usable() const { return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE; }

      private:
        HRESULT result_;
    } com;
    if (!com.usable())
        return QImage();

    using Microsoft::WRL::ComPtr;
    ComPtr<IWICImagingFactory> factory;
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&factory))))
        return QImage();

    ComPtr<IWICBitmapDecoder> decoder;
    if (FAILED(factory->CreateDecoderFromFilename(
            filenameWide.c_str(), nullptr, GENERIC_READ, WICDecodeMetadataCacheOnDemand,
            &decoder)))
        return QImage();

    ComPtr<IWICBitmapFrameDecode> frame;
    if (FAILED(decoder->GetFrame(0, &frame)))
        return QImage();

    UINT fullWidth = 0;
    UINT fullHeight = 0;
    if (FAILED(frame->GetSize(&fullWidth, &fullHeight)) || fullWidth == 0 || fullHeight == 0 ||
        fullWidth > static_cast<UINT>(std::numeric_limits<int>::max()) ||
        fullHeight > static_cast<UINT>(std::numeric_limits<int>::max()))
        return QImage();

    const int x0 = std::max(0, rawRect.x());
    const int y0 = std::max(0, rawRect.y());
    const int x1 = std::min(static_cast<int>(fullWidth), rawRect.x() + rawRect.width());
    const int y1 = std::min(static_cast<int>(fullHeight), rawRect.y() + rawRect.height());
    if (x1 <= x0 || y1 <= y0)
        return QImage();

    ComPtr<IWICBitmapSource> source;
    if (x0 == 0 && y0 == 0 && x1 == static_cast<int>(fullWidth) &&
        y1 == static_cast<int>(fullHeight))
    {
        source = frame;
    }
    else
    {
        ComPtr<IWICBitmapClipper> clipper;
        const WICRect clipRect{x0, y0, x1 - x0, y1 - y0};
        if (FAILED(factory->CreateBitmapClipper(&clipper)) ||
            FAILED(clipper->Initialize(frame.Get(), &clipRect)))
            return QImage();
        source = clipper;
    }

    ComPtr<IWICBitmapScaler> scaler;
    if (FAILED(factory->CreateBitmapScaler(&scaler)) ||
        FAILED(scaler->Initialize(source.Get(), static_cast<UINT>(target.width()),
                                  static_cast<UINT>(target.height()), WICBitmapInterpolationModeFant)))
        return QImage();

    ComPtr<IWICFormatConverter> converter;
    if (FAILED(factory->CreateFormatConverter(&converter)) ||
        FAILED(converter->Initialize(scaler.Get(), GUID_WICPixelFormat24bppBGR,
                                     WICBitmapDitherTypeNone, nullptr, 0.0,
                                     WICBitmapPaletteTypeCustom)))
        return QImage();

    const qint64 outputBytes = static_cast<qint64>(target.width()) * target.height() * 3;
    if (outputBytes <= 0 || outputBytes > std::numeric_limits<UINT>::max())
        return QImage();
    QImage output(target, QImage::Format_RGB888);
    if (output.isNull())
        return QImage();
    const UINT stride = static_cast<UINT>(target.width() * 3);
    const UINT byteCount = stride * static_cast<UINT>(target.height());
    std::vector<uint8_t> bgr(byteCount);
    if (FAILED(converter->CopyPixels(nullptr, stride, byteCount, bgr.data())))
        return QImage();
    for (int y = 0; y < target.height(); ++y)
    {
        const uint8_t *src = bgr.data() + static_cast<size_t>(y) * stride;
        uchar *dst = output.scanLine(y);
        for (int x = 0; x < target.width(); ++x)
        {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }

    QImageReader metadataReader(filename);
    metadataReader.setAutoTransform(true);
    fillMetadata(metadataReader, output, meta);
    if (meta.filePath.empty())
        meta.filePath = path;
    meta.fileSize = QFileInfo(filename).size();
    meta.width = static_cast<int>(fullWidth);
    meta.height = static_cast<int>(fullHeight);
    meta.orientation = 1;
    meta.format = "TIFF";
    return output;
}

#endif

#if !defined(Q_OS_WIN)
QImage decodeTiffWic(const std::string &, const QRect &, const QSize &,
                     mviewer::domain::ImageMetadata &)
{
    return QImage();
}
#endif

// Container format: prefer the reader's reported format; fall back to the file
// extension (QImageReader leaves format() empty for some formats, e.g. BMP).
std::string formatName(const QImageReader &reader)
{
    const QByteArray fmt = reader.format();
    if (!fmt.isEmpty())
        return QString::fromLatin1(fmt).toUpper().toStdString();
    const QString ext = QFileInfo(reader.fileName()).suffix().toLower();
    if (ext == "jpg" || ext == "jpeg")
        return "JPEG";
    if (ext == "png")
        return "PNG";
    if (ext == "bmp")
        return "BMP";
    if (ext == "tif" || ext == "tiff")
        return "TIFF";
    if (!ext.isEmpty())
        return ext.toUpper().toStdString();
    return std::string();
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

    meta.orientation = orientationFromTransform(reader.transformation());

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

    meta.format = formatName(reader);
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
    QString ext = QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size())))
                      .suffix()
                      .toLower();
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
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
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
    return decodeLod(path, maxEdge, outMeta);
}

std::vector<std::string> QtDecoder::extensions() const
{
    return supportedExts();
}

// ── M47 source-backed capabilities ───────────────────────────────────────────

bool QtDecoder::canProbe(const std::string &path) const
{
    return canDecode(path);
}

bool QtDecoder::probeMetadata(const std::string &path,
                              mviewer::domain::ImageMetadata &meta) const
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty())
        return false;
    const QFileInfo info(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    if (meta.filePath.empty())
        meta.filePath = path;
    meta.fileName = info.fileName().toUtf8().toStdString();
    meta.fileSize = info.size();
    meta.modifiedEpochSec = info.lastModified().toSecsSinceEpoch();
    meta.width = full.width();
    meta.height = full.height();
    // Report the DISPLAYED geometry (EXIF applied), matching the decodeFull /
    // decodeLod metadata semantics.
    const auto t = reader.transformation();
    if (transformSwapsDimensions(t))
        std::swap(meta.width, meta.height);
    meta.orientation = orientationFromTransform(t);
    meta.format = formatName(reader);

    // M48 Phase 1: the display ICC metadata must be stable on the probe (the
    // placeholder carries it), not only on a decode. A tiny scaled read pulls
    // the ICC out of the container header without materializing pixels. This
    // is only attempted for formats whose scaled read is truly bounded (JPEG
    // DCT); for others the profile is discovered at decode time instead.
    const QString ext = info.suffix().toLower();
    if (ext == "jpg" || ext == "jpeg" || ext == "png")
    {
        QImageReader iccReader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
        iccReader.setScaledSize(QSize(1, 1));
        QImage tiny;
        if (iccReader.read(&tiny) && tiny.colorSpace().isValid())
        {
            const QByteArray icc = tiny.colorSpace().iccProfile();
            if (!icc.isEmpty())
            {
                meta.hasIccProfile = true;
                const QByteArray encoded = icc.toBase64();
                meta.textKeys["MViewer.DisplayICC.Base64"] =
                    std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
            }
        }
    }
    return true;
}

bool QtDecoder::canNativeLod(const std::string &path) const
{
    // Evidence-based (M47/M53): JPEG uses the decoder's reduced DCT read. TIFF
    // uses the measured strip/clip adapter below; it never asks Qt to allocate
    // the full raster. Other formats remain on the honest scaled fallback.
    const QString ext = QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size())))
                            .suffix()
                            .toLower();
    if (ext == "jpg" || ext == "jpeg")
        return true;
    if (!isTiffPath(path))
        return false;
#if !defined(Q_OS_WIN)
    return false;
#else
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    return reader.size().isValid() && !transformSwapsDimensions(reader.transformation()) &&
           reader.transformation() == QImageIOHandler::TransformationNone;
#endif
}

bool QtDecoder::canNativeRegion(const std::string &path) const
{
    (void)path;
    // Qt offers no true random-access tile decode. decodeRegion() is the
    // bounded-memory clipRect path, classified BoundedRasterRegion by the
    // provider — never claimed as native.
    return false;
}

ImageData QtDecoder::decodeLod(const std::string &path, int maxEdge,
                               mviewer::domain::ImageMetadata &outMeta) const
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty())
        return ImageData();
    if (outMeta.filePath.empty())
        outMeta.filePath = path;
    outMeta.fileSize = QFileInfo(QString::fromUtf8(path.data(), static_cast<int>(path.size())))
                           .size();

#if defined(Q_OS_WIN)
    if (isTiffPath(path) && reader.transformation() == QImageIOHandler::TransformationNone)
    {
        const int sourceEdge = std::max(full.width(), full.height());
        const int requestedEdge = std::min(sourceEdge, std::max(1, maxEdge));
        const double ratio = static_cast<double>(requestedEdge) / sourceEdge;
        const QSize target(std::max(1, static_cast<int>(std::lround(full.width() * ratio))),
                           std::max(1, static_cast<int>(std::lround(full.height() * ratio))));
        const QImage img = decodeTiffWic(path, QRect(0, 0, full.width(), full.height()), target,
                                         outMeta);
        if (!img.isNull())
            return toImageData(img);
        return ImageData();
    }
#endif

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
    if (transformSwapsDimensions(transform))
        std::swap(outMeta.width, outMeta.height);
    return toImageData(img);
}

ImageData QtDecoder::decodeRegion(const std::string &path, int x, int y, int w, int h,
                                  int targetW, int targetH,
                                  mviewer::domain::ImageMetadata &meta) const
{
    QImageReader reader(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    reader.setAutoTransform(true);
    const QSize full = reader.size();
    if (!full.isValid() || full.isEmpty() || w <= 0 || h <= 0)
        return ImageData();

#if defined(Q_OS_WIN)
    if (isTiffPath(path) && reader.transformation() == QImageIOHandler::TransformationNone)
    {
        const int x0 = std::max(0, x);
        const int y0 = std::max(0, y);
        const int x1 = std::min(full.width(), x + w);
        const int y1 = std::min(full.height(), y + h);
        if (x1 <= x0 || y1 <= y0 || targetW <= 0 || targetH <= 0)
            return ImageData();
        const QImage img = decodeTiffWic(path, QRect(x0, y0, x1 - x0, y1 - y0),
                                         QSize(targetW, targetH), meta);
        return toImageData(img);
    }
#endif

    // The clip rect is in source (pre-transform) coordinates per Qt semantics.
    reader.setClipRect(QRect(x, y, w, h));
    if (targetW > 0 && targetH > 0)
        reader.setScaledSize(QSize(targetW, targetH));
    const QImage img = reader.read();
    if (img.isNull())
        return ImageData();
    if (meta.filePath.empty())
        meta.filePath = path;
    meta.width = full.width();
    meta.height = full.height();
    const auto t = reader.transformation();
    if (transformSwapsDimensions(t))
        std::swap(meta.width, meta.height);
    meta.orientation = orientationFromTransform(t);
    meta.format = formatName(reader);
    return toImageData(img);
}
