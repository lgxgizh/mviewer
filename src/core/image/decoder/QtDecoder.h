#pragma once

#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// Qt-based decoder for raster formats supported by QImageReader
// (JPEG/PNG/BMP/TIFF). This is the primary decoder; DecoderRegistry tries it
// before the fallback. It respects EXIF orientation (auto-transform) and
// outputs RGB24 ImageData, identical to the legacy Decoder output.
class QtDecoder : public IDecoder
{
  public:
    bool canDecode(const std::string &path) const override;
    ImageData decodeFull(const std::string &path) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge) const override;
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override;
    std::vector<std::string> extensions() const override;
    const char *name() const override
    {
        return "QtDecoder";
    }
};
