#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include "domain/DecodeError.h"
#include "domain/Result.h"

#include <string>
#include <vector>

// Thin delegating shim over DecoderRegistry. Kept so existing callers keep
// compiling after the single Decoder was split into per-format decoders.
// All real decode logic now lives in core/image/decoder/*.
class Decoder
{
  public:
    // Full-image decode
    static ImageData decodeFull(const std::string &path);

    // Modern Result-based decode returning rich error context when decode fails.
    static mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
    decodeFullResult(const std::string &path);
    static mviewer::domain::Result<ImageData, mviewer::domain::DecodeError>
    decodeFullResult(const std::string &path, mviewer::domain::ImageMetadata &outMeta);

    // Decode scaled to within maxEdge (preserve aspect ratio)
    static ImageData decodeScaled(const std::string &path, int maxEdge);
    static ImageData decodeScaled(const std::string &path, int maxEdge,
                                  mviewer::domain::ImageMetadata &outMeta);

    // Full-image decode and fill metadata (M6 extension)
    static ImageData decodeFull(const std::string &path, mviewer::domain::ImageMetadata &outMeta);

    // Supported image suffixes (kept consistent with UI filtering)
    static std::vector<std::string> supportedExtensions();
};
