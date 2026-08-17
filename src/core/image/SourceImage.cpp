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
                         std::shared_ptr<IDecoder> decoder, ISourceImageCapabilities *caps)
    : m_path(std::move(path)), m_valid(valid), m_meta(std::move(meta)),
      m_decoder(std::move(decoder)), m_caps(caps)
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

    return std::shared_ptr<SourceImage>(
        new SourceImage(path, true, std::move(meta), std::move(decoder), caps));
}

ImageData SourceImage::decodeLod(int maxEdge)
{
    auto &stats = SourceDecodeStats::instance().counters();
    mviewer::domain::ImageMetadata meta = m_meta;
    ImageData out;

    if (m_caps != nullptr)
    {
        if (m_caps->canNativeLod(m_path))
        {
            out = m_caps->decodeLod(m_path, maxEdge, meta);
            m_lastPath = SourceDecodePath::NativeLod;
            if (!out.isNull())
            {
                stats.nativeLod.fetch_add(1);
                m_meta = std::move(meta);
                return out;
            }
            stats.failed.fetch_add(1);
            return ImageData{};
        }
        // Backend exists but no native LOD: its scaled decode may still be a
        // bounded read (JPEG) or a full-raster read (TIFF) — we cannot prove
        // which, so the honest classification is the fallback one. The
        // classification records the ATTEMPT (counters increment even when the
        // attempt fails, e.g. the TIFF allocation-limit rejection).
        out = m_caps->decodeLod(m_path, maxEdge, meta);
        m_lastPath = SourceDecodePath::FullDecodeScaled;
        stats.fullDecodeScaled.fetch_add(1);
        if (!out.isNull())
        {
            m_meta = std::move(meta);
            return out;
        }
        stats.failed.fetch_add(1);
        return ImageData{};
    }

    // No capability interface: compatible fallback via the existing shim.
    stats.fallbackNoCapability.fetch_add(1);
    out = Decoder::decodeScaled(m_path, maxEdge, meta);
    m_lastPath = SourceDecodePath::FullDecodeScaled;
    stats.fullDecodeScaled.fetch_add(1);
    if (!out.isNull())
    {
        m_meta = std::move(meta);
        return out;
    }
    stats.failed.fetch_add(1);
    return ImageData{};
}

ImageData SourceImage::decodeRegion(int x, int y, int w, int h, int targetW, int targetH)
{
    auto &stats = SourceDecodeStats::instance().counters();
    mviewer::domain::ImageMetadata meta = m_meta;

    if (m_caps != nullptr)
    {
        if (m_caps->canNativeRegion(m_path))
        {
            ImageData out = m_caps->decodeRegion(m_path, x, y, w, h, targetW, targetH, meta);
            m_lastPath = SourceDecodePath::NativeRegion;
            if (!out.isNull())
            {
                stats.nativeRegion.fetch_add(1);
                return out;
            }
            stats.failed.fetch_add(1);
            return ImageData{};
        }
        // Bounded-memory region (e.g. Qt clipRect): memory bounded by the
        // region, CPU possibly full-image. NOT a true native region decode.
        ImageData out = m_caps->decodeRegion(m_path, x, y, w, h, targetW, targetH, meta);
        m_lastPath = SourceDecodePath::BoundedRasterRegion;
        if (!out.isNull())
        {
            stats.boundedRegion.fetch_add(1);
            return out;
        }
        stats.failed.fetch_add(1);
        return ImageData{};
    }

    // No capability interface: full decode + client crop + scale.
    stats.fallbackNoCapability.fetch_add(1);
    m_lastPath = SourceDecodePath::FullDecodeCrop;
    stats.fullDecodeCrop.fetch_add(1);
    ImageData full = Decoder::decodeFull(m_path, meta);
    if (full.isNull())
    {
        stats.failed.fetch_add(1);
        return ImageData{};
    }
    stats.fullDecode.fetch_add(1);
    // Clamp to source bounds (same semantics as cropRegion).
    const int x0 = std::max(0, x);
    const int y0 = std::max(0, y);
    const int x1 = std::min(full.width, x + w);
    const int y1 = std::min(full.height, y + h);
    const int cw = x1 - x0;
    const int ch = y1 - y0;
    if (cw <= 0 || ch <= 0)
    {
        stats.failed.fetch_add(1);
        return ImageData{};
    }
    const mviewer::domain::Selection sel{x0, y0, cw, ch};
    ImageData crop = cropRegion(full, sel);
    if (crop.isNull())
    {
        stats.failed.fetch_add(1);
        return ImageData{};
    }
    return scaleNearest(crop, targetW, targetH);
}

} // namespace mviewer::core
