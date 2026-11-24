#pragma once

// Internal helpers shared between QtDecoder.cpp and QtDecoder_meta.cpp.
// Not part of the decoder's public interface.
//
// Qt-free on purpose: this lives under src/core/, and R5 allows only
// QtConvert.h / QtMetadataSemantics.h to include Qt there. The helpers take
// references and a format value, so forward declarations are enough.

#include <string>

#include "domain/Image.h"

class QImage;
class QImageReader;

namespace mviewer::core::detail
{

/// Format name for a file, preferring the reader's reported format and falling
/// back to the extension (QImageReader leaves format() empty for some formats).
std::string decoderFormatName(const QImageReader &reader);

/// Fill the raster-derived metadata fields (geometry, orientation, channel /
/// bit depth, colour space) exactly as the Qt decode path reports them.
/// `sourceFormat` is `int(QImage::Format)`; 0 / -1 means "unknown".
void decoderFillMetadata(const QImageReader &reader, int sourceFormat, const QImage &img,
                         mviewer::domain::ImageMetadata &meta);

} // namespace mviewer::core::detail
