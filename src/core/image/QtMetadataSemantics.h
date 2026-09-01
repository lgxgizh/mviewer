#pragma once

// Shared Qt-internal metadata semantics.  Public domain/core headers remain
// Qt-free; only decoder/reader translation units include this file.

#include "core/image/IccProfile.h"
#include "domain/Image.h"

#include <QColorSpace>
#include <QImage>
#include <QImageIOHandler>
#include <QImageReader>

#include <algorithm>

namespace mviewer::core::qtmetadata
{

inline int orientationFromTransform(QImageIOHandler::Transformations transform)
{
    if (transform == QImageIOHandler::TransformationNone)
        return 1;
    if (transform == QImageIOHandler::TransformationMirror)
        return 2;
    if (transform == QImageIOHandler::TransformationRotate180)
        return 3;
    if (transform == QImageIOHandler::TransformationFlip)
        return 4;
    if (transform == QImageIOHandler::TransformationFlipAndRotate90)
        return 5; // transpose
    if (transform == QImageIOHandler::TransformationRotate90)
        return 6;
    if (transform == QImageIOHandler::TransformationMirrorAndRotate90)
        return 7; // transverse
    if (transform == QImageIOHandler::TransformationRotate270)
        return 8;
    return 1;
}

inline bool transformSwapsDimensions(QImageIOHandler::Transformations transform)
{
    return transform == QImageIOHandler::TransformationRotate90 ||
           transform == QImageIOHandler::TransformationRotate270 ||
           transform == QImageIOHandler::TransformationMirrorAndRotate90 ||
           transform == QImageIOHandler::TransformationFlipAndRotate90;
}

inline int channelsForFormat(QImage::Format format, const QImage &image)
{
    switch (format)
    {
    case QImage::Format_Mono:
    case QImage::Format_MonoLSB:
    case QImage::Format_Grayscale8:
    case QImage::Format_Grayscale16:
        return 1;
    case QImage::Format_RGB32:
    case QImage::Format_RGB16:
    case QImage::Format_RGB666:
    case QImage::Format_RGB555:
    case QImage::Format_RGB888:
    case QImage::Format_BGR888:
    case QImage::Format_RGBX8888:
    case QImage::Format_RGBX64:
        return 3;
    case QImage::Format_ARGB32:
    case QImage::Format_ARGB32_Premultiplied:
    case QImage::Format_RGBA8888:
    case QImage::Format_RGBA8888_Premultiplied:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA16FPx4:
    case QImage::Format_RGBA32FPx4:
        return 4;
    default:
        return image.hasAlphaChannel() ? 4 : 3;
    }
}

inline int bitsPerChannel(QImage::Format format, int channels)
{
    switch (format)
    {
    case QImage::Format_Mono:
    case QImage::Format_MonoLSB:
        return 1;
    case QImage::Format_RGB16:
    case QImage::Format_RGB555:
        return 5;
    case QImage::Format_RGB666:
        return 6;
    case QImage::Format_Grayscale16:
    case QImage::Format_RGBX64:
    case QImage::Format_RGBA64:
    case QImage::Format_RGBA16FPx4:
        return 16;
    case QImage::Format_RGBA32FPx4:
        return 32;
    default:
        // RGB32/ARGB32 are packed 32-bit storage with 8-bit channels; using
        // depth()/channels here is the historical M59 bug (32/3 == 10).
        return channels > 0 ? 8 : 0;
    }
}

inline std::string colorSpaceLabel(const QColorSpace &colorSpace)
{
    if (!colorSpace.isValid())
        return "unknown";
    switch (colorSpace.primaries())
    {
    case QColorSpace::Primaries::SRgb:
        return "sRGB";
    case QColorSpace::Primaries::AdobeRgb:
        return "AdobeRGB";
    case QColorSpace::Primaries::DciP3D65:
        return "DisplayP3";
    default:
        return "unknown";
    }
}

inline void applyColorMetadata(const QImage &image, mviewer::domain::ImageMetadata &meta)
{
    const QColorSpace colorSpace = image.colorSpace();
    meta.colorSpace = colorSpaceLabel(colorSpace);
    meta.hasIccProfile = false;
    if (!colorSpace.isValid())
        return;

    const QByteArray profile = colorSpace.iccProfile();
    if (profile.isEmpty())
        return;

    meta.hasIccProfile = true;
    const QByteArray encoded = profile.toBase64();
    meta.textKeys["MViewer.DisplayICC.Base64"] =
        std::string(encoded.constData(), static_cast<size_t>(encoded.size()));
    const auto parsed =
        parseIccProfile(reinterpret_cast<const unsigned char *>(profile.constData()),
                        static_cast<size_t>(profile.size()));
    meta.iccDescription = parsed.description;
    meta.iccCopyright = parsed.copyright;
    meta.iccColorSpace = parsed.colorSpace;
    meta.iccDeviceClass = parsed.deviceClass;
    meta.iccPcs = parsed.pcs;
    meta.iccRenderingIntent = parsed.renderingIntent;
    meta.iccVersion = parsed.version;
}

inline void applyRasterMetadata(const QImage &image, mviewer::domain::ImageMetadata &meta)
{
    meta.channels = channelsForFormat(image.format(), image);
    meta.bitDepth = bitsPerChannel(image.format(), meta.channels);
    applyColorMetadata(image, meta);
}

inline void applyReaderRasterMetadata(const QImageReader &reader, const QImage &image,
                                      mviewer::domain::ImageMetadata &meta)
{
    // imageFormat() describes the source format even when read() materializes
    // a normalized 32-bit QImage.  Prefer it so 16-bit TIFF/PNG sources do not
    // get reported as 8-bit merely because the display raster is normalized.
    const QImage::Format sourceFormat = reader.imageFormat();
    if (sourceFormat != QImage::Format_Invalid)
    {
        const QImage probe(1, 1, sourceFormat);
        meta.channels = channelsForFormat(sourceFormat, probe);
        meta.bitDepth = bitsPerChannel(sourceFormat, meta.channels);
        applyColorMetadata(image, meta);
        return;
    }
    applyRasterMetadata(image, meta);
}

} // namespace mviewer::core::qtmetadata
