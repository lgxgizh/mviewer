#pragma once

#include "core/image/ExifOrientation.h"
#include "core/image/ISourceImageCapabilities.h"
#include "core/image/ImageBuffer.h"
#include "core/image/decoder/IDecoder.h"
#include "domain/Image.h"

#include <atomic>
#include <memory>
#include <string>

namespace mviewer::core
{

// M47 RFC section 3.1: the decode-path classification. Every source-backed
// operation records exactly one path so tests can prove what really ran and
// whether a "no full materialization" claim is true.
enum class SourceDecodePath
{
    ProbeMetadata,      // metadata only, no pixels
    NativeLod,          // backend reduced-resolution decode (no full raster)
    NativeRegion,       // backend true random-access region decode
    BoundedRasterRegion, // bounded-memory partial raster during decode (e.g. Qt
                         // clipRect: memory bounded by the region, CPU may be
                         // full-image) — NOT a true native region decode
    FullDecodeScaled,   // full decode then client-scale (fallback LOD)
    FullDecodeCrop      // full decode then client-crop (fallback region)
};

// Thread-safe, process-wide instrumentation proving which paths ran. Empty in
// production; tests read the counters (M46-style instrumentation pattern).
struct SourceDecodeCounters
{
    std::atomic<uint64_t> probe{0};          // metadata probes (any mechanism)
    std::atomic<uint64_t> nativeLod{0};      // NativeLod classifications
    std::atomic<uint64_t> nativeRegion{0};   // NativeRegion classifications
    std::atomic<uint64_t> boundedRegion{0};  // BoundedRasterRegion classifications
    std::atomic<uint64_t> fullDecodeScaled{0}; // FullDecodeScaled classifications
    std::atomic<uint64_t> fullDecodeCrop{0};   // FullDecodeCrop classifications
    std::atomic<uint64_t> fullDecode{0};       // actual full-resolution decodes
                                               // that ran (decodeFull calls)
    std::atomic<uint64_t> failed{0};           // failed operations
    std::atomic<uint64_t> fallbackNoCapability{0}; // provider fallback path used
                                                   // (decoder without the interface)

    void reset()
    {
        probe.store(0);
        nativeLod.store(0);
        nativeRegion.store(0);
        boundedRegion.store(0);
        fullDecodeScaled.store(0);
        fullDecodeCrop.store(0);
        fullDecode.store(0);
        failed.store(0);
        fallbackNoCapability.store(0);
    }
};

class SourceDecodeStats
{
  public:
    static SourceDecodeStats &instance();

    SourceDecodeCounters &counters()
    {
        return m_counters;
    }
    const SourceDecodeCounters &counters() const
    {
        return m_counters;
    }

  private:
    SourceDecodeStats() = default;
    SourceDecodeCounters m_counters;
};

// M47: source-backed image abstraction. `SourceImage` represents a file as a
// metadata + capability handle: opening it NEVER decodes pixels, and display
// requests (LOD/region) go through the capability path when the decoder offers
// one, otherwise through the compatible full-decode fallback. Every operation
// records its classification in SourceDecodeStats.
//
// M48 Phase 1: every decode returns an atomic SourceRasterResult — the pixels
// AND the complete metadata (ICC, orientation, dims) as-of-this-decode travel
// together, so a consumer never has to read mutable metadata before/after a
// decode and guess whether it changed. SourceImage NEVER mutates its own probe
// metadata on decode (no implicit metadata mutation); the result carries the
// authoritative metadata.
//
// Coordinate contract: decodeRegion() consumes RAW source coordinates (pre
// EXIF transform); the result's coveredRect is in the declared source space.
// See ExifOrientation.h for the single mapping between raw and displayed
// coordinates (EXIF orientation 1-8). decodeLod() always covers the FULL
// oriented source.
//
// By design SourceImage retains NO pixel buffers: the display pipeline decides
// what to keep (viewport LOD/tile), the analysis pipeline decides when to
// materialize full source. This is the RFC's display/analysis separation.
class SourceImage
{
  public:
    // Open a source file WITHOUT decoding pixels. `path` is resolved through
    // the frozen DecoderRegistry (first claiming decoder, same order the
    // registry uses); the capability interface is discovered by dynamic_cast,
    // so non-capability decoders fall back cleanly. Returns nullptr when the
    // file cannot be probed at all (missing/unsupported).
    static std::shared_ptr<SourceImage> open(const std::string &path);

    ~SourceImage() = default;

    // Cheap metadata (no pixel decode was performed by open()).
    const mviewer::domain::ImageMetadata &metadata() const
    {
        return m_meta;
    }
    bool isValid() const
    {
        return m_valid;
    }

    // Capability truth (from the backend; false for fallback sources).
    bool hasNativeLod() const
    {
        return m_caps != nullptr && m_caps->canNativeLod(m_path);
    }
    bool hasNativeRegion() const
    {
        return m_caps != nullptr && m_caps->canNativeRegion(m_path);
    }
    bool hasCapabilities() const
    {
        return m_caps != nullptr;
    }

    // The RAW source dimensions (pre EXIF transform) and the displayed ones.
    int rawWidth() const
    {
        return m_rawW;
    }
    int rawHeight() const
    {
        return m_rawH;
    }
    int displayWidth() const
    {
        return m_meta.width;
    }
    int displayHeight() const
    {
        return m_meta.height;
    }
    int orientation() const
    {
        return m_meta.orientation;
    }

    // M48: decoded-pixel + authoritative-metadata result, returned atomically.
    struct RasterResult
    {
        bool ok = false;
        SourceDecodePath decodePath = SourceDecodePath::ProbeMetadata;
        ImageData pixels;                    // never the full source unless explicitly asked
        mviewer::domain::ImageMetadata metadata; // complete as of THIS decode (ICC, orientation,
                                                 // dims); independent of the live probe metadata
        SourceRect coveredRect;              // in `space` coordinates
        SourceCoordinateSpace space = SourceCoordinateSpace::Raw;
    };

    // Viewport LOD over the FULL oriented source: longest edge <= maxEdge,
    // aspect preserved, EXIF applied. Never materializes more than
    // ~maxEdge^2*3 bytes on the native path. Result.coveredRect is the full
    // raw source; space = Raw.
    RasterResult decodeLod(int maxEdge);

    // Bounded region decode of the RAW source rect (pre-EXIF coordinates),
    // EXIF-applied output scaled to (targetW,targetH). Exact pixel values are
    // only guaranteed when hasNativeRegion(); otherwise the result is a
    // display representation (see RFC) — exact-source consumers must NOT use
    // this path. Result.coveredRect is the clamped raw rect; space = Raw.
    RasterResult decodeRegion(const SourceRect &rawRect, int targetW, int targetH);

    // The classification recorded by the most recent decodeLod/decodeRegion.
    SourceDecodePath lastPath() const
    {
        return m_lastPath;
    }

    const std::string &path() const
    {
        return m_path;
    }

  private:
    SourceImage(std::string path, bool valid, mviewer::domain::ImageMetadata meta, int rawW,
                int rawH, std::shared_ptr<IDecoder> decoder, ISourceImageCapabilities *caps);

    std::string m_path;
    bool m_valid = false;
    mviewer::domain::ImageMetadata m_meta;
    int m_rawW = 0;
    int m_rawH = 0;
    std::shared_ptr<IDecoder> m_decoder; // keeps the decoder alive (may be null)
    ISourceImageCapabilities *m_caps = nullptr; // owned by m_decoder when set
    SourceDecodePath m_lastPath = SourceDecodePath::ProbeMetadata;
};

} // namespace mviewer::core
