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

struct FalseColorLUT
{
    uint8_t r[256];
    uint8_t g[256];
    uint8_t b[256];

    constexpr FalseColorLUT() : r{}, g{}, b{}
    {
        for (int l = 0; l < 256; ++l)
        {
            const float t = static_cast<float>(l) / 255.0f;
            auto fclamp = [](float val) {
                return val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val);
            };
            auto fabs_ = [](float val) {
                return val < 0.0f ? -val : val;
            };
            const float fr = fclamp(1.5f - fabs_(4.0f * t - 3.0f));
            const float fg = fclamp(1.5f - fabs_(4.0f * t - 2.0f));
            const float fb = fclamp(1.5f - fabs_(4.0f * t - 1.0f));
            r[l] = static_cast<uint8_t>(255.0f * fr);
            g[l] = static_cast<uint8_t>(255.0f * fg);
            b[l] = static_cast<uint8_t>(255.0f * fb);
        }
    }
};

inline constexpr FalseColorLUT s_falseColorLut{};

template <int Cpp, bool IsBgr>
inline void applyFalseColorImpl(const ImageBuffer &v)
{
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * Cpp;
            uint8_t r, g, b;
            if constexpr (IsBgr)
            {
                b = p[0];
                g = p[1];
                r = p[2];
            }
            else
            {
                r = p[0];
                g = p[1];
                b = p[2];
            }
            const int l = luminance(r, g, b);
            const uint8_t idx = static_cast<uint8_t>(std::clamp(l, 0, 255));
            if constexpr (IsBgr)
            {
                p[0] = s_falseColorLut.b[idx];
                p[1] = s_falseColorLut.g[idx];
                p[2] = s_falseColorLut.r[idx];
            }
            else
            {
                p[0] = s_falseColorLut.r[idx];
                p[1] = s_falseColorLut.g[idx];
                p[2] = s_falseColorLut.b[idx];
            }
        }
    }
}

inline void applyFalseColor(ImageData &img)
{
    const ImageBuffer v = img.view();
    switch (v.format)
    {
    case PixelFormat::RGB24:
        applyFalseColorImpl<3, false>(v);
        break;
    case PixelFormat::RGBA32:
        applyFalseColorImpl<4, false>(v);
        break;
    case PixelFormat::BGR24:
        applyFalseColorImpl<3, true>(v);
        break;
    case PixelFormat::BGRA32:
        applyFalseColorImpl<4, true>(v);
        break;
    case PixelFormat::Grayscale8:
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
                row[x] = s_falseColorLut.r[row[x]];
        }
        break;
    default:
        break;
    }
}

template <int Cpp, bool IsBgr>
inline void applyChannelImpl(const ImageBuffer &v, OverlayMode mode)
{
    switch (mode)
    {
    case OverlayMode::ChannelR:
    {
        constexpr int rIdx = IsBgr ? 2 : 0;
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
            {
                uint8_t *p = row + static_cast<size_t>(x) * Cpp;
                const uint8_t r = p[rIdx];
                p[0] = r;
                p[1] = r;
                p[2] = r;
            }
        }
        break;
    }
    case OverlayMode::ChannelG:
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
            {
                uint8_t *p = row + static_cast<size_t>(x) * Cpp;
                const uint8_t g = p[1];
                p[0] = g;
                p[1] = g;
                p[2] = g;
            }
        }
        break;
    }
    case OverlayMode::ChannelB:
    {
        constexpr int bIdx = IsBgr ? 0 : 2;
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
            {
                uint8_t *p = row + static_cast<size_t>(x) * Cpp;
                const uint8_t b = p[bIdx];
                p[0] = b;
                p[1] = b;
                p[2] = b;
            }
        }
        break;
    }
    case OverlayMode::ChannelV:
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
            {
                uint8_t *p = row + static_cast<size_t>(x) * Cpp;
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        break;
    }
    case OverlayMode::ChannelY:
    default:
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
            {
                uint8_t *p = row + static_cast<size_t>(x) * Cpp;
                uint8_t r, g, b;
                if constexpr (IsBgr)
                {
                    b = p[0];
                    g = p[1];
                    r = p[2];
                }
                else
                {
                    r = p[0];
                    g = p[1];
                    b = p[2];
                }
                const uint8_t yVal = static_cast<uint8_t>(std::clamp(luminance(r, g, b), 0, 255));
                p[0] = yVal;
                p[1] = yVal;
                p[2] = yVal;
            }
        }
        break;
    }
    }
}

inline void applyChannelOverlay(ImageData &img, OverlayMode mode)
{
    if (img.isNull() || img.format == PixelFormat::Grayscale8)
        return;
    const ImageBuffer v = img.view();
    switch (v.format)
    {
    case PixelFormat::RGB24:
        applyChannelImpl<3, false>(v, mode);
        break;
    case PixelFormat::RGBA32:
        applyChannelImpl<4, false>(v, mode);
        break;
    case PixelFormat::BGR24:
        applyChannelImpl<3, true>(v, mode);
        break;
    case PixelFormat::BGRA32:
        applyChannelImpl<4, true>(v, mode);
        break;
    default:
        break;
    }
}

template <int Cpp, bool IsBgr>
inline void applyZebraImpl(const ImageBuffer &v, int lo, int hi)
{
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            if (((x + y) & 7) >= 4)
                continue;
            uint8_t *p = row + static_cast<size_t>(x) * Cpp;
            uint8_t r, g, b;
            if constexpr (IsBgr)
            {
                b = p[0];
                g = p[1];
                r = p[2];
            }
            else
            {
                r = p[0];
                g = p[1];
                b = p[2];
            }
            const int l = luminance(r, g, b);
            if (l >= hi || l <= lo)
            {
                const uint8_t v0 = (l >= hi) ? 0 : 255;
                p[0] = v0;
                p[1] = v0;
                p[2] = v0;
            }
        }
    }
}

inline void applyZebraGrayscale(const ImageBuffer &v, int lo, int hi)
{
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            if (((x + y) & 7) >= 4)
                continue;
            const int l = row[x];
            if (l >= hi || l <= lo)
                row[x] = (l >= hi) ? 0 : 255;
        }
    }
}

inline void applyZebraOverlay(ImageData &img, int zebraThresholdPct)
{
    const int thr = std::clamp(zebraThresholdPct, 1, 40);
    const int lo = (thr * 255) / 100;
    const int hi = 255 - lo;
    const ImageBuffer v = img.view();
    switch (v.format)
    {
    case PixelFormat::RGB24:
        applyZebraImpl<3, false>(v, lo, hi);
        break;
    case PixelFormat::RGBA32:
        applyZebraImpl<4, false>(v, lo, hi);
        break;
    case PixelFormat::BGR24:
        applyZebraImpl<3, true>(v, lo, hi);
        break;
    case PixelFormat::BGRA32:
        applyZebraImpl<4, true>(v, lo, hi);
        break;
    case PixelFormat::Grayscale8:
        applyZebraGrayscale(v, lo, hi);
        break;
    default:
        break;
    }
}

// Apply an overlay in place on an ImageData. Supports RGB24 / RGBA32 /
// BGR24 / BGRA32 / Grayscale8. Alpha is preserved for the *A variants.
// Safe to call every frame: it operates on the passed buffer only.
inline void applyOverlay(ImageData &img, OverlayMode mode, int zebraThresholdPct)
{
    if (mode == OverlayMode::None || img.isNull())
        return;

    if (mode == OverlayMode::FalseColor)
    {
        applyFalseColor(img);
        return;
    }

    if (isChannelOverlay(mode))
    {
        applyChannelOverlay(img, mode);
        return;
    }

    if (mode == OverlayMode::Zebra)
    {
        applyZebraOverlay(img, zebraThresholdPct);
    }
}

} // namespace mviewer
