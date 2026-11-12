#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/LumaMatrix.h"

#include <algorithm>
#include <cstdint>

namespace mviewer
{

// F4 (M22): live analysis overlays for the zoomable ImageViewer.
// Kept in core (pure std) so both the viewer and the standalone
// AnalysisOverlayDialog can share identical pixel math.
enum class OverlayMode : std::uint8_t
{
    None = 0,
    Zebra = 1,      // over/under-exposure clip indicators
    FalseColor = 2, // jet-mapped luminance
    ChannelR = 3,   // grayscale of the red plane
    ChannelG = 4,   // grayscale of the green plane
    ChannelB = 5,   // grayscale of the blue plane
    ChannelY = 6,   // grayscale of source Y (Rec.601 unless MViewer.Yuv.Matrix is set)
    ChannelV = 7    // grayscale of HSV-V (max(R,G,B))
};

inline bool isChannelOverlay(OverlayMode mode)
{
    return mode == OverlayMode::ChannelR || mode == OverlayMode::ChannelG ||
           mode == OverlayMode::ChannelB || mode == OverlayMode::ChannelY ||
           mode == OverlayMode::ChannelV;
}

inline const char *overlayModeLabel(OverlayMode mode)
{
    switch (mode)
    {
    case OverlayMode::Zebra:
        return "Zebra";
    case OverlayMode::FalseColor:
        return "FalseColor";
    case OverlayMode::ChannelR:
        return "R";
    case OverlayMode::ChannelG:
        return "G";
    case OverlayMode::ChannelB:
        return "B";
    case OverlayMode::ChannelY:
        return "Y";
    case OverlayMode::ChannelV:
        return "V";
    case OverlayMode::None:
    default:
        return "RGB";
    }
}

// ChannelY uses `matrix` (Rec.601 when omitted). Other planes ignore it.
inline uint8_t
channelPlaneValue(OverlayMode mode, int r, int g, int b,
                  mviewer::core::yuv::Matrix matrix = mviewer::core::yuv::Matrix::BT601)
{
    switch (mode)
    {
    case OverlayMode::ChannelR:
        return static_cast<uint8_t>(r);
    case OverlayMode::ChannelG:
        return static_cast<uint8_t>(g);
    case OverlayMode::ChannelB:
        return static_cast<uint8_t>(b);
    case OverlayMode::ChannelV:
        return static_cast<uint8_t>(std::max({r, g, b}));
    case OverlayMode::ChannelY:
    default:
        return lumaMatrix8(r, g, b, matrix);
    }
}

// In-place overlay kernels (implemented in ImageOverlay.cpp). Each supports
// RGB24 / RGBA32 / BGR24 / BGRA32; FalseColor and Zebra also Grayscale8.
void applyFalseColor(ImageData &img);
// `matrix` affects ChannelY only. Zebra and false color stay Rec.601: they
// mark displayed brightness, not the source Y plane.
void applyChannelOverlay(ImageData &img, OverlayMode mode,
                         mviewer::core::yuv::Matrix matrix = mviewer::core::yuv::Matrix::BT601);
void applyZebraOverlay(ImageData &img, int zebraThresholdPct);

// Apply an overlay in place on an ImageData. Supports RGB24 / RGBA32 /
// BGR24 / BGRA32 / Grayscale8. Alpha is preserved for the *A variants.
// Safe to call every frame: it operates on the passed buffer only.
// ChannelY uses `matrix` (default Rec.601). Zebra and false color ignore it.
void applyOverlay(ImageData &img, OverlayMode mode, int zebraThresholdPct,
                  mviewer::core::yuv::Matrix matrix = mviewer::core::yuv::Matrix::BT601);

} // namespace mviewer
