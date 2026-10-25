#pragma once

// How a RAW display request should be satisfied. Preview (embedded JPEG) stays
// the fast path when it already covers the request. LibRaw half-size is the
// reduced demosaic when the preview is too small and the request is at most
// half the sensor edge. Full demosaic is never selected here — decodeLod must
// not silently materialize the full raster.

#include "domain/Image.h"

#include <cstdint>
#include <string>

namespace mviewer::core
{

enum class RawLodIntent : std::uint8_t
{
    Preview, // embedded JPEG is adequate (edge >= request, or any preview for a full ask)
    TryHalf, // preview is short; caller may half-size demosaic, else fall back
    Empty    // no honest reduced path (do not full-demosaic inside decodeLod)
};

// `previewEdge` / `requestedEdge` are longest-edge pixels. requestedEdge <= 0
// means "full display" — a preview, when present, is adequate and half-size
// is not a substitute for a true full decode.
inline RawLodIntent chooseRawLod(int previewEdge, int requestedEdge, bool libraw)
{
    if (requestedEdge <= 0)
        return previewEdge > 0 ? RawLodIntent::Preview : RawLodIntent::Empty;
    if (previewEdge >= requestedEdge)
        return RawLodIntent::Preview;
    if (libraw)
        return RawLodIntent::TryHalf;
    if (previewEdge > 0)
        return RawLodIntent::Preview;
    return RawLodIntent::Empty;
}

// True when a half-size demosaic is the right reduced raster: the request is
// at most half the sensor's long edge ("maxEdge << full").
inline bool rawHalfCoversRequest(int sensorEdge, int requestedEdge)
{
    if (sensorEdge <= 1 || requestedEdge <= 0)
        return false;
    return requestedEdge * 2 <= sensorEdge;
}

enum class RawFullIntent : std::uint8_t
{
    Preview,
    FullDemosaic,
    Empty
};

// PreferPreview keeps today's fast path (embedded preview whenever one exists).
// AutoFullWhenPreviewSmall demosaics when the preview long edge is under 90%
// of the sensor long edge. AlwaysFull demosaics whenever LibRaw can.
enum class RawDisplayPolicy : std::uint8_t
{
    PreferPreview = 0,
    AutoFullWhenPreviewSmall = 1,
    AlwaysFull = 2
};

inline bool rawFullDemosaicPossible(bool libraw, long long fileBytes)
{
    return libraw && fileBytes >= 4096;
}

// `sensorEdge` <= 0 means the sensor size is unknown. Auto then matches
// PreferPreview so a missing sensor probe cannot force a full demosaic.
inline RawFullIntent chooseRawFull(int previewEdge, int sensorEdge, bool libraw,
                                   long long fileBytes, RawDisplayPolicy policy)
{
    const bool hasPreview = previewEdge > 0;
    const bool canFull = rawFullDemosaicPossible(libraw, fileBytes);
    if (policy == RawDisplayPolicy::AlwaysFull)
    {
        if (canFull)
            return RawFullIntent::FullDemosaic;
        return hasPreview ? RawFullIntent::Preview : RawFullIntent::Empty;
    }
    if (policy == RawDisplayPolicy::AutoFullWhenPreviewSmall && sensorEdge > 0)
    {
        const bool previewSmall =
            static_cast<double>(previewEdge) < 0.9 * static_cast<double>(sensorEdge);
        if (previewSmall)
        {
            if (canFull)
                return RawFullIntent::FullDemosaic;
            return hasPreview ? RawFullIntent::Preview : RawFullIntent::Empty;
        }
    }
    if (hasPreview)
        return RawFullIntent::Preview;
    if (canFull)
        return RawFullIntent::FullDemosaic;
    return RawFullIntent::Empty;
}

inline RawDisplayPolicy rawDisplayPolicyFromInt(int value)
{
    if (value == static_cast<int>(RawDisplayPolicy::AutoFullWhenPreviewSmall))
        return RawDisplayPolicy::AutoFullWhenPreviewSmall;
    if (value == static_cast<int>(RawDisplayPolicy::AlwaysFull))
        return RawDisplayPolicy::AlwaysFull;
    return RawDisplayPolicy::PreferPreview;
}

// Process-wide preference. Default PreferPreview. Not read from QSettings on
// the decode path — the UI copies the setting in at startup and on accept.
void setRawDisplayPolicy(RawDisplayPolicy policy);
RawDisplayPolicy rawDisplayPolicy();

// Per-path override for an explicit "Decode Full RAW". Survives a successful
// decode so later loads of the same path reuse the full frame. Cleared on cancel.
void requestRawFullDecode(const std::string &path);
void clearRawFullDecode(const std::string &path);
bool rawFullDecodeRequested(const std::string &path);

// Empty unless the path is RAW and the decision differs from historical
// PreferPreview. ImageRepository appends it so default cache keys stay identical.
std::string rawCacheKeySuffix(const std::string &path);
bool rawCacheKeyAgrees(const std::string &key, const std::string &suffix);

// Remember the source chosen for a cache key so a later disk hit can label it.
void rememberRawSource(const std::string &cacheKey, mviewer::domain::RawSource source);
mviewer::domain::RawSource recalledRawSource(const std::string &cacheKey);

} // namespace mviewer::core
