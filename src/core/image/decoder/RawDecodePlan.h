#pragma once

// How a RAW display request should be satisfied. Preview (embedded JPEG) stays
// the fast path when it already covers the request. LibRaw half-size is the
// reduced demosaic when the preview is too small and the request is at most
// half the sensor edge. Full demosaic is never selected here — decodeLod must
// not silently materialize the full raster.

namespace mviewer::core
{

enum class RawLodIntent
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

enum class RawFullIntent
{
    Preview,
    FullDemosaic,
    Empty
};

// Full decode: keep a usable embedded preview. Demosaic only when that preview
// is missing and LibRaw is linked. `fileBytes` rejects tiny fixtures that are
// not real RAW containers.
inline RawFullIntent chooseRawFull(int previewEdge, bool libraw, long long fileBytes)
{
    if (previewEdge > 0)
        return RawFullIntent::Preview;
    if (libraw && fileBytes >= 4096)
        return RawFullIntent::FullDemosaic;
    return RawFullIntent::Empty;
}

} // namespace mviewer::core
