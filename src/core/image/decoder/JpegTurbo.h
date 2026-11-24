#pragma once

// libjpeg-turbo JPEG fast path (optional dependency, see cmake/LibJpegTurbo.cmake).
//
// Qt's JPEG plugin decodes into a QImage (RGB32) and the pipeline then copies
// that into ImageData (RGB24): two full-image passes. libjpeg-turbo writes the
// destination format directly and uses a SIMD IDCT, and it can emit 1/2, 1/4
// and 1/8 scale output natively, so the LOD path needs no second rescale.
//
// Every entry point is safe to call in a build without libjpeg-turbo: they
// return "not available" / a null image and the caller falls back to Qt.

#include <string>

#include "core/image/ImageBuffer.h"

namespace mviewer::core::detail
{

/// True when this build linked libjpeg-turbo.
bool hasTurboJpeg();

/// Decode a JPEG straight into an ImageData, bypassing QImage.
///
/// `maxEdge <= 0` requests full resolution; otherwise libjpeg-turbo's scaled
/// IDCT picks the largest built-in factor whose output still covers the edge.
///
/// Returns a null ImageData when the file is not a baseline RGB / greyscale
/// JPEG, when libjpeg-turbo rejects it, or when this build has no fast path -
/// the caller then uses Qt's decoder, so a null return is never a user-visible
/// failure.
ImageData decodeJpegTurbo(const std::string &path, int maxEdge);

} // namespace mviewer::core::detail
