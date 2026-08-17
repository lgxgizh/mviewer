#pragma once

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

    // Viewport LOD: longest edge <= maxEdge, aspect preserved, EXIF applied.
    // Never materializes more than ~maxEdge^2*3 bytes on the native path.
    ImageData decodeLod(int maxEdge);

    // Bounded region: source rect scaled to target size. Exact pixel values
    // are only guaranteed when hasNativeRegion(); otherwise the result is a
    // display representation (see RFC) — exact-source consumers must NOT use
    // this path.
    ImageData decodeRegion(int x, int y, int w, int h, int targetW, int targetH);

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
    SourceImage(std::string path, bool valid, mviewer::domain::ImageMetadata meta,
                std::shared_ptr<IDecoder> decoder, ISourceImageCapabilities *caps);

    std::string m_path;
    bool m_valid = false;
    mviewer::domain::ImageMetadata m_meta;
    std::shared_ptr<IDecoder> m_decoder; // keeps the decoder alive (may be null)
    ISourceImageCapabilities *m_caps = nullptr; // owned by m_decoder when set
    SourceDecodePath m_lastPath = SourceDecodePath::ProbeMetadata;
};

} // namespace mviewer::core
