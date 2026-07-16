#pragma once

#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// Last-resort decoder. canDecode() always returns true so that DecoderRegistry
// falls through to it only after every specific decoder declines. It attempts a
// generic QImageReader decode; if that fails the decode returns an empty
// ImageData (graceful failure, no crash). Used for formats Qt can read but we
// have no dedicated decoder for (e.g. WEBP, GIF, XPM).
class QtFallbackDecoder : public IDecoder
{
public:
    bool canDecode(const std::string& path) const override;
    ImageData decodeFull(const std::string& path) const override;
    ImageData decodeScaled(const std::string& path, int maxEdge) const override;
    ImageData decodeFull(const std::string& path,
                                  mviewer::domain::ImageMetadata& outMeta) const override;
    std::vector<std::string> extensions() const override;
    const char* name() const override { return "QtFallbackDecoder"; }
};
