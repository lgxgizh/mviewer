#pragma once

#include "domain/Image.h"
#include "core/image/ImageBuffer.h"

#include <string>
#include <vector>

// Decoder interface (Qt-free header). Concrete decoders live in
// core/image/decoder/ and are dispatched by DecoderRegistry.
// All decode work happens off the UI thread; implementations may use Qt
// internally in their .cpp, but the interface exposes only std types.
class IDecoder
{
public:
    virtual ~IDecoder() = default;

    // True if this decoder claims the given file (by extension or content).
    virtual bool canDecode(const std::string& path) const = 0;

    // Full-resolution decode -> always RGB24 ImageData (or null on failure).
    virtual ImageData decodeFull(const std::string& path) const = 0;

    // Scaled decode: longest edge clamped to maxEdge, keeping aspect ratio.
    virtual ImageData decodeScaled(const std::string& path, int maxEdge) const = 0;

    // Decode + metadata in one pass (avoids re-opening the file). Decoders that
    // cannot determine a field leave it at its default. Never throws.
    virtual ImageData decodeFull(const std::string& path,
                                 mviewer::domain::ImageMetadata& outMeta) const = 0;

    // Lowercased extensions this decoder handles (e.g. "jpg","png").
    virtual std::vector<std::string> extensions() const = 0;

    // Human-readable name (for diagnostics).
    virtual const char* name() const = 0;
};
