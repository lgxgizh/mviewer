// M47 Phase 1 — SourceImage provider: resolves a path to a capability-bearing
// decoder WITHOUT modifying the frozen DecoderRegistry, and maps every
// operation to the RFC classification. Qt-free (pure std); decoders may use
// Qt internally.
#include "core/image/SourceImage.h"

#include "core/image/Decoder.h"
#include "core/image/MetadataReader.h"
#include "core/image/decoder/DecoderRegistry.h"

#include <algorithm>
#include <cstdint>

namespace mviewer::core
{

SourceDecodeStats &SourceDecodeStats::instance()
{
    static SourceDecodeStats stats;
    return stats;
}

namespace
{

// The first decoder that claims `path`, in registry order (the same dispatch
// order the frozen registry uses). Returns nullptr when nothing claims it.
std::shared_ptr<IDecoder> claimingDecoder(const std::string &path)
{
    auto &registry = DecoderRegistry::instance();
    for (const auto &id : registry.available())
    {
        auto decoder = registry.get(id);
        if (decoder && decoder->canDecode(path))
            return decoder;
    }
    return nullptr;
}

// Nearest-neighbor scale (Qt-free, deterministic). Used only by the
// no-capability fallback path; the capability path scales inside the backend.
ImageData scaleNearest(const ImageData &src, int targetW, int targetH)
{
    if (src.isNull() || targetW <= 0 || targetH <= 0)
        return ImageData{};
    if (src.width == targetW && src.height == targetH)
        return src;
    const int cpp = src.channelsPerPixel();
    ImageData dst = makeImageData(targetW, targetH, src.format);
    const ImageBuffer v = src.view();
    const ImageBuffer dv = dst.view();
    for (int y = 0; y < targetH; ++y)
    {
        const int sy = std::min(src.height - 1, (y * src.height) / targetH);
        const uint8_t *sp = v.data + static_cast<size_t>(sy) * v.stride();
        uint8_t *dp = dv.data + static_cast<size_t>(y) * dv.stride();
        for (int x = 0; x < targetW; ++x)
        {
            const int sx = std::min(src.width - 1, (x * src.width) / targetW);
            const uint8_t *p = sp + static_cast<size_t>(sx) * cpp;
            uint8_t *q = dp + static_cast<size_t>(x) * cpp;
            for (int c = 0; c < cpp; ++c)
                q[c] = p[c];
        }
    }
    return dst;
}

} // namespace

SourceImage::SourceImage(std::string path, bool valid, mviewer::domain::ImageMetadata meta,
                         int rawW, int rawH, std::shared_ptr<IDecoder> decoder,
                         ISourceImageCapabilities *caps)
    : m_path(std::move(path)), m_valid(valid), m_meta(std::move(meta)), m_rawW(rawW),
      m_rawH(rawH), m_decoder(std::move(decoder)), m_caps(caps)
{
}

std::shared_ptr<SourceImage> SourceImage::open(const std::string &path)
{
    auto decoder = claimingDecoder(path);
    if (!decoder)
        return nullptr;
    auto *caps = dynamic_cast<ISourceImageCapabilities *>(decoder.get());

    mviewer::domain::ImageMetadata meta;
    meta.filePath = path;

    auto &stats = SourceDecodeStats::instance().counters();
    stats.probe.fetch_add(1);
    bool probed = false;
    if (caps && caps->canProbe(path))
        probed = caps->probeMetadata(path, meta);
    if (!probed || meta.width <= 0 || meta.height <= 0)
    {
        // Fallback probe: the repository metadata path, which also never
        // decodes pixels (QImageReader::size() header read).
        meta = MetadataReader::read(path);
        if (meta.filePath.empty())
            meta.filePath = path;
        probed = meta.width > 0 && meta.height > 0;
    }
    if (!probed)
        return nullptr;

    // M48 Phase 1: a source-backed placeholder must not depend on the caller
    // reading mutable metadata at a specific time. Guarantee the stable
    // identity fields (fileName, modified time) regardless of which probe ran
    // (the capability probe may not fill them), so the placeholder is always
    // complete. (fileName is derived here; modifiedEpochSec is filled by the
    // backend probe where it can — a decoder that cannot answer leaves it at
    // its default.)
    if (meta.fileName.empty())
    {
        const std::string::size_type slash = path.find_last_of("/\\");
        meta.fileName = slash == std::string::npos ? path : path.substr(slash + 1);
    }

    // Raw (pre-EXIF) geometry: the probe reports the DISPLAYED dims; for the
    // swap orientations the raw dimensions are the transposed display dims.
    int rawW = meta.width;
    int rawH = meta.height;
    if (meta.orientation == 5 || meta.orientation == 6 || meta.orientation == 7 ||
        meta.orientation == 8)
    {
        rawW = meta.height;
        rawH = meta.width;
    }

    return std::shared_ptr<SourceImage>(
        new SourceImage(path, true, std::move(meta), rawW, rawH, std::move(decoder), caps));
}

SourceImage::RasterResult SourceImage::decodeLod(int maxEdge)
{
    auto &stats = SourceDecodeStats::instance().counters();
    // Copy the probe metadata; the backend may enrich the COPY (ICC, format,
    // dims) but SourceImage itself never mutates m_meta (M48: no implicit
    // metadata mutation; callers get the authoritative metadata in the result).
    mviewer::domain::ImageMetadata meta = m_meta;
    RasterResult r;
    r.space = SourceCoordinateSpace::Raw;
    r.coveredRect = {0, 0, m_rawW, m_rawH};

    if (m_caps != nullptr)
    {
        if (m_caps->canNativeLod(m_path))
        {
            r.pixels = m_caps->decodeLod(m_path, maxEdge, meta);
            r.decodePath = SourceDecodePath::NativeLod;
            m_lastPath = SourceDecodePath::NativeLod;
            if (!r.pixels.isNull())
            {
                stats.nativeLod.fetch_add(1);
                r.metadata = std::move(meta);
                r.ok = true;
                return r;
            }
            stats.failed.fetch_add(1);
            return r;
        }
        // Backend exists but no native LOD: its scaled decode may still be a
        // bounded read (JPEG) or a full-raster read (TIFF) — we cannot prove
        // which, so the honest classification is the fallback one. The
        // classification records the ATTEMPT (counters increment even when the
        // attempt fails, e.g. the TIFF allocation-limit rejection).
        r.pixels = m_caps->decodeLod(m_path, maxEdge, meta);
        r.decodePath = SourceDecodePath::FullDecodeScaled;
        m_lastPath = SourceDecodePath::FullDecodeScaled;
        stats.fullDecodeScaled.fetch_add(1);
        if (!r.pixels.isNull())
        {
            r.metadata = std::move(meta);
            r.ok = true;
            return r;
        }
        stats.failed.fetch_add(1);
        return r;
    }

    // No capability interface: compatible fallback via the existing shim.
    stats.fallbackNoCapability.fetch_add(1);
    r.pixels = Decoder::decodeScaled(m_path, maxEdge, meta);
    r.decodePath = SourceDecodePath::FullDecodeScaled;
    m_lastPath = SourceDecodePath::FullDecodeScaled;
    stats.fullDecodeScaled.fetch_add(1);
    if (!r.pixels.isNull())
    {
        r.metadata = std::move(meta);
        r.ok = true;
        return r;
    }
    stats.failed.fetch_add(1);
    return r;
}

SourceImage::RasterResult SourceImage::decodeRegion(const SourceRect &rawRect, int targetW,
                                                    int targetH)
{
    auto &stats = SourceDecodeStats::instance().counters();
    mviewer::domain::ImageMetadata meta = m_meta;
    RasterResult r;
    r.space = SourceCoordinateSpace::Raw;
    // Clamp the requested raw rect to the raw source bounds and report the
    // ACTUAL covered area.
    const int x0 = std::max(0, rawRect.x);
    const int y0 = std::max(0, rawRect.y);
    const int x1 = std::min(m_rawW, rawRect.x + rawRect.w);
    const int y1 = std::min(m_rawH, rawRect.y + rawRect.h);
    r.coveredRect = {x0, y0, std::max(0, x1 - x0), std::max(0, y1 - y0)};
    if (r.coveredRect.isNull())
    {
        stats.failed.fetch_add(1);
        return r;
    }
    const int cx = r.coveredRect.x;
    const int cy = r.coveredRect.y;
    const int cw = r.coveredRect.w;
    const int ch = r.coveredRect.h;

    if (m_caps != nullptr)
    {
        if (m_caps->canNativeRegion(m_path))
        {
            r.pixels = m_caps->decodeRegion(m_path, cx, cy, cw, ch, targetW, targetH, meta);
            r.decodePath = SourceDecodePath::NativeRegion;
            m_lastPath = SourceDecodePath::NativeRegion;
            if (!r.pixels.isNull())
            {
                stats.nativeRegion.fetch_add(1);
                r.metadata = std::move(meta);
                r.ok = true;
                return r;
            }
            stats.failed.fetch_add(1);
            return r;
        }
        // Bounded-memory region (e.g. Qt clipRect): memory bounded by the
        // region, CPU possibly full-image. NOT a true native region decode.
        r.pixels = m_caps->decodeRegion(m_path, cx, cy, cw, ch, targetW, targetH, meta);
        r.decodePath = SourceDecodePath::BoundedRasterRegion;
        m_lastPath = SourceDecodePath::BoundedRasterRegion;
        if (!r.pixels.isNull())
        {
            stats.boundedRegion.fetch_add(1);
            r.metadata = std::move(meta);
            r.ok = true;
            return r;
        }
        stats.failed.fetch_add(1);
        return r;
    }

    // No capability interface: full decode + client crop + scale.
    stats.fallbackNoCapability.fetch_add(1);
    r.decodePath = SourceDecodePath::FullDecodeCrop;
    m_lastPath = SourceDecodePath::FullDecodeCrop;
    stats.fullDecodeCrop.fetch_add(1);
    ImageData full = Decoder::decodeFull(m_path, meta);
    if (full.isNull())
    {
        stats.failed.fetch_add(1);
        return r;
    }
    stats.fullDecode.fetch_add(1);
    const int fw = full.width;
    const int fh = full.height;
    const int fx0 = std::max(0, cx);
    const int fy0 = std::max(0, cy);
    const int fx1 = std::min(fw, cx + cw);
    const int fy1 = std::min(fh, cy + ch);
    const int ccw = fx1 - fx0;
    const int cch = fy1 - fy0;
    if (ccw <= 0 || cch <= 0)
    {
        stats.failed.fetch_add(1);
        return r;
    }
    const mviewer::domain::Selection sel{fx0, fy0, ccw, cch};
    ImageData crop = cropRegion(full, sel);
    if (crop.isNull())
    {
        stats.failed.fetch_add(1);
        return r;
    }
    r.pixels = scaleNearest(crop, targetW, targetH);
    if (r.pixels.isNull())
    {
        stats.failed.fetch_add(1);
        return r;
    }
    r.metadata = std::move(meta);
    r.ok = true;
    return r;
}

} // namespace mviewer::core
