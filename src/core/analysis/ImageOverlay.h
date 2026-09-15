#pragma once

#include "core/image/ImageBuffer.h"

#include <algorithm>
#include <cmath>
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
    ChannelY = 6,   // grayscale of BT.601 luminance
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

inline uint8_t channelPlaneValue(OverlayMode mode, int r, int g, int b)
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
        return static_cast<uint8_t>(std::clamp(
            luminance(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)), 0,
            255));
    }
}

inline void applyChannelOverlay(ImageData &img, OverlayMode mode)
{
    const int cpp = img.channelsPerPixel();
    const ImageBuffer v = img.view();
    const bool bgr = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * cpp;
            int r, g, b;
            if (v.format == PixelFormat::Grayscale8)
                r = g = b = p[0];
            else if (bgr)
                b = p[0], g = p[1], r = p[2];
            else
                r = p[0], g = p[1], b = p[2];
            const uint8_t plane = channelPlaneValue(mode, r, g, b);
            if (v.format == PixelFormat::Grayscale8)
                p[0] = plane;
            else
                p[0] = plane, p[1] = plane, p[2] = plane;
        }
    }
}

// Apply an overlay in place on an ImageData. Supports RGB24 / RGBA32 /
// BGR24 / BGRA32 / Grayscale8. Alpha is preserved for the *A variants.
// Safe to call every frame: it operates on the passed buffer only.
inline void applyOverlay(ImageData &img, OverlayMode mode, int zebraThresholdPct)
{
    if (mode == OverlayMode::None || img.isNull())
        return;
    const int cpp = img.channelsPerPixel();
    const ImageBuffer v = img.view();
    const bool bgr = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);

    if (mode == OverlayMode::FalseColor)
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
            {
                uint8_t *p = row + static_cast<size_t>(x) * cpp;
                int r, g, b;
                if (v.format == PixelFormat::Grayscale8)
                    r = g = b = p[0];
                else if (bgr)
                    b = p[0], g = p[1], r = p[2];
                else
                    r = p[0], g = p[1], b = p[2];
                const int l = luminance(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                        static_cast<uint8_t>(b));
                const float t = std::clamp(static_cast<float>(l) / 255.f, 0.f, 1.f);
                const float fr = std::clamp(1.5f - std::fabs(4.f * t - 3.f), 0.f, 1.f);
                const float fg = std::clamp(1.5f - std::fabs(4.f * t - 2.f), 0.f, 1.f);
                const float fb = std::clamp(1.5f - std::fabs(4.f * t - 1.f), 0.f, 1.f);
                const uint8_t R = static_cast<uint8_t>(255 * fr);
                const uint8_t G = static_cast<uint8_t>(255 * fg);
                const uint8_t B = static_cast<uint8_t>(255 * fb);
                if (bgr)
                    p[0] = B, p[1] = G, p[2] = R;
                else
                    p[0] = R, p[1] = G, p[2] = B;
            }
        }
        return;
    }

    if (isChannelOverlay(mode))
    {
        applyChannelOverlay(img, mode);
        return;
    }

    if (mode != OverlayMode::Zebra)
        return;

    // Zebra: paint clip indicators only on a 4/8 diagonal stripe so the
    // underlying image stays readable.
    const int thr = std::clamp(zebraThresholdPct, 1, 40);
    const int lo = (thr * 255) / 100;
    const int hi = 255 - lo;
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * cpp;
            int r, g, b;
            if (v.format == PixelFormat::Grayscale8)
                r = g = b = p[0];
            else if (bgr)
                b = p[0], g = p[1], r = p[2];
            else
                r = p[0], g = p[1], b = p[2];
            const int l = luminance(static_cast<uint8_t>(r), static_cast<uint8_t>(g),
                                    static_cast<uint8_t>(b));
            if (l >= hi || l <= lo)
            {
                if (((x + y) % 8) < 4)
                {
                    const uint8_t v0 = (l >= hi) ? 0 : 255;
                    p[0] = v0;
                    p[1] = v0;
                    p[2] = v0;
                }
            }
        }
    }
}

} // namespace mviewer
