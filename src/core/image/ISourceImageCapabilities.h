#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <string>

namespace mviewer::core
{

// M47 RFC (docs/rfc/M47_SOURCE_BACKED_DISPLAY.md) section 3: OPTIONAL, additive
// capability interface for source-backed display. A decoder that supports any
// of these implements this interface on top of the frozen IDecoder contract;
// discovery is dynamic_cast-based inside SourceImageProvider, so a decoder
// that does NOT implement it keeps working through the compatible fallback
// path (decodeScaled/decodeFull). Every method here is best-effort and never
// throws: returning false/empty means "not available for this path", which the
// provider maps to the FullDecode* fallback classification.
//
// The interface deliberately has NO `cancel` method yet: cancellation lives at
// the orchestration layer (the worker/request machinery that Phase 2 adds),
// not inside the stateless decoder contract.
class ISourceImageCapabilities
{
  public:
    virtual ~ISourceImageCapabilities() = default;

    // True when probeMetadata() can answer WITHOUT decoding pixels (e.g. via
    // the container header / QImageReader::size()).
    virtual bool canProbe(const std::string & /*path*/) const
    {
        return false;
    }

    // Metadata without pixel decode. Populates width/height/orientation/format
    // (and any other fields the backend can answer cheaply). Returns false when
    // the backend cannot answer (provider then falls back to the repository
    // metadata path, which never decodes pixels either).
    virtual bool probeMetadata(const std::string &path, mviewer::domain::ImageMetadata &meta) const
    {
        (void)path;
        (void)meta;
        return false;
    }

    // True when decodeLod() avoids materializing the full-resolution raster
    // (a genuine reduced-resolution decode, e.g. libjpeg DCT scaling via
    // QImageReader::setScaledSize). Evidence-based per format; never claimed
    // without proof.
    virtual bool canNativeLod(const std::string &path) const
    {
        (void)path;
        return false;
    }

    // True when decodeRegion() is a true native random-access region decode
    // (strip/tile-aware). Deliberately conservative: false unless the backend
    // really provides random tile access.
    virtual bool canNativeRegion(const std::string & /*path*/) const
    {
        return false;
    }

    // Reduced-resolution decode: longest edge clamped to maxEdge, aspect
    // preserved, EXIF applied. Empty ImageData on failure. May be called even
    // when canNativeLod() is false (the provider classifies the outcome); when
    // it is false the implementation may still answer cheaply or not at all.
    virtual ImageData decodeLod(const std::string &path, int maxEdge,
                                mviewer::domain::ImageMetadata &meta) const
    {
        (void)path;
        (void)maxEdge;
        (void)meta;
        return ImageData{};
    }

    // Region decode: the source rect (x,y,w,h) scaled to (targetW,targetH).
    // Bounded-memory when the backend supports clipping during decode; may be
    // a full-raster read otherwise. Empty ImageData on failure.
    virtual ImageData decodeRegion(const std::string &path, int x, int y, int w, int h, int targetW,
                                   int targetH, mviewer::domain::ImageMetadata &meta) const
    {
        (void)path;
        (void)x;
        (void)y;
        (void)w;
        (void)h;
        (void)targetW;
        (void)targetH;
        (void)meta;
        return ImageData{};
    }
};

// Additive truth seam for the non-native region path. Implementing the base
// capability interface alone no longer implies bounded-memory source pixels.
enum class SourceRegionBehavior
{
    BoundedSourcePixels,
    MayMaterializeFullRaster
};

class ISourceImageRegionTruth
{
  public:
    virtual ~ISourceImageRegionTruth() = default;
    virtual SourceRegionBehavior sourceRegionBehavior(const std::string &path) const = 0;
};

} // namespace mviewer::core
