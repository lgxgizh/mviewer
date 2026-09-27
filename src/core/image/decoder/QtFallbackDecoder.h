#pragma once

#include "core/image/ISourceImageCapabilities.h"
#include "core/image/decoder/IDecoder.h"

#include <string>
#include <vector>

// Last-resort decoder. canDecode() always returns true so that DecoderRegistry
// falls through to it only after every specific decoder declines. It attempts a
// generic QImageReader decode; if that fails the decode returns an empty
// ImageData (graceful failure, no crash). Used for formats Qt can read but we
// have no dedicated decoder for (e.g. WEBP, GIF, XPM).
//
// Implements ISourceImageCapabilities with an honest canNativeLod=false: plugin
// handlers may allocate a full raster before honoring setScaledSize. decodeLod
// still uses the scaled-reader path so SourceImage can classify FullDecodeScaled
// without a second full-then-downscale in the UI.
class QtFallbackDecoder : public IDecoder, public mviewer::core::ISourceImageCapabilities
{
  public:
    bool canDecode(const std::string &path) const override;
    ImageData decodeFull(const std::string &path) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge) const override;
    ImageData decodeScaled(const std::string &path, int maxEdge,
                           mviewer::domain::ImageMetadata &outMeta) const override;
    ImageData decodeFull(const std::string &path,
                         mviewer::domain::ImageMetadata &outMeta) const override;
    std::vector<std::string> extensions() const override;
    const char *name() const override
    {
        return "QtFallbackDecoder";
    }

    bool canProbe(const std::string &path) const override;
    bool probeMetadata(const std::string &path,
                       mviewer::domain::ImageMetadata &meta) const override;
    // Always false: fallback/plugin formats lack a proven native reduced decode.
    bool canNativeLod(const std::string &path) const override;
    bool canNativeRegion(const std::string &path) const override;
    ImageData decodeLod(const std::string &path, int maxEdge,
                        mviewer::domain::ImageMetadata &meta) const override;
};
