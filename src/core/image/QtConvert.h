#pragma once
// Internal core helper: ImageData <-> QImage conversion.
// This header is one of the adapter headers in core **explicitly allowed to include Qt** (the other
// is QtMetadataSemantics.h, see docs/adr/011 and R5 of architecture_gate.ps1). It may be included
// by core .cpp files and UI private headers (*_p.h); other than that, core/domain Header files must
// remain free of Qt includes.
#include "core/image/DisplayColorContext.h"
#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <QImage>

namespace mvcore
{

// ImageData -> QImage (format-aware: Grayscale8/RGB24/RGBA32 each mapped separately)
QImage toQImage(const ImageData &src);

// M28 P1-03: Zero-copy QImage view over the ImageData buffer where the byte
// order matches a Qt format (RGB24->RGB888, BGR24->BGR888, BGRA32->ARGB32,
// Grayscale8->Grayscale8). The underlying buffer is kept alive via a custom
// cleanup hook until the returned QImage and all copies are destroyed. Returns
// a null QImage for formats whose byte order does not map (RGBA32); callers
// must fall back to toQImage().
QImage toQImageRef(const ImageData &src);

// Returns zero-copy toQImageRef(src) if format allows, else falls back to toQImage(src).
inline QImage toQImageFast(const ImageData &src)
{
    const QImage ref = toQImageRef(src);
    return ref.isNull() ? toQImage(src) : ref;
}

// Materialize a display-only sRGB copy from analysis-domain pixels. Embedded
// ICC is applied to the copy; the source ImageData bytes are never modified.
QImage toDisplayQImage(const ImageData &src, const mviewer::domain::ImageMetadata &meta);

// Materialize a display copy for an explicit presentation target.  The source
// profile is converted directly to the target (never source -> sRGB -> target)
// and the source ImageData remains untouched.
QImage toDisplayQImage(const ImageData &src, const mviewer::domain::ImageMetadata &meta,
                       const mviewer::core::DisplayColorContext &target);

// Materialize the same display transform into an ImageData tile. This keeps
// CPU QPainter and GPU upload paths byte-equivalent while preserving the
// source ImageFrame as analysis-domain data.
ImageData toDisplayImageData(const ImageData &src, const mviewer::domain::ImageMetadata &meta);

ImageData toDisplayImageData(const ImageData &src, const mviewer::domain::ImageMetadata &meta,
                             const mviewer::core::DisplayColorContext &target);

// QImage -> ImageData (format-aware: Grayscale8 is kept as grayscale, the rest are converted to
// RGB24)
ImageData fromQImage(const QImage &src);

// QImage -> ImageData (preserve the Alpha channel: convert to RGBA32 / BGRA32 when Alpha is
// present, equivalent to fromQImage when there is no Alpha)
ImageData fromQImagePreservingAlpha(const QImage &src);

} // namespace mvcore
