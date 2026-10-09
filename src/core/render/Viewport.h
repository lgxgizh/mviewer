#pragma once

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

// ─── Viewport ────────────────────────────────────────────────────────────────
// Domain-free view transform: maps between *image space* (full-resolution pixel
// coordinates) and *screen space* (widget pixels). Owns pan/zoom only — no Qt,
// no rasterization. The Widget is the Viewport's owner; the Renderer consumes
// the regions the Viewport reports as visible.
//
// This is the foundation of the Render Pipeline (Image -> Tile -> Viewport ->
// Renderer -> Widget): large images (100MP, RAW) are never fully rasterized
// into one bitmap; the Viewport decides which source tiles are on screen and
// the Renderer draws only those.

enum class FitPolicy
{
    // Browse fullscreen presentation: use every available client pixel in the
    // limiting dimension while preserving the source aspect ratio.
    MaximizeClient,
    // Normal-window presentation: retain a small visual breathing room.
    Comfortable,
};

inline constexpr double kMaxFitMargin = 1.0;
inline constexpr double kComfortFitMargin = 0.95;

// Floor one screen-space edge into an int. Shared by both sides of a tile
// boundary so neighbours land on the same pixel instead of rounding apart.
inline int snapScreenEdge(double value)
{
    if (!std::isfinite(value))
        return 0;
    const double floored = std::floor(value);
    if (floored >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    if (floored <= static_cast<double>(std::numeric_limits<int>::min()))
        return std::numeric_limits<int>::min();
    return static_cast<int>(floored);
}

struct Viewport
{
    // Screen-space size of the viewport (widget client area), in pixels.
    int screenW = 0;
    int screenH = 0;

    // Uniform zoom factor: image_px = screen_px / scale.
    double scale = 1.0;

    // Screen-space translation (top-left of the image in widget coords).
    double offsetX = 0.0;
    double offsetY = 0.0;

    Viewport() = default;
    Viewport(int sw, int sh, double s, double ox, double oy)
        : screenW(sw), screenH(sh), scale(s), offsetX(ox), offsetY(oy)
    {
    }

    // Fit an (iw x ih) image centered inside the screen with `margin` padding.
    void fit(int imageW, int imageH, FitPolicy policy)
    {
        fit(imageW, imageH,
            policy == FitPolicy::MaximizeClient ? kMaxFitMargin : kComfortFitMargin);
    }

    // Numeric overload retained for core callers/tests that intentionally
    // specify a custom margin. UI presentation code should use FitPolicy.
    void fit(int imageW, int imageH, double margin = kComfortFitMargin)
    {
        if (imageW <= 0 || imageH <= 0 || screenW <= 0 || screenH <= 0)
        {
            scale = 1.0;
            offsetX = 0.0;
            offsetY = 0.0;
            return;
        }
        const double m = (margin > 0.0 && std::isfinite(margin)) ? margin : kComfortFitMargin;
        const double sx = static_cast<double>(screenW) / static_cast<double>(imageW);
        const double sy = static_cast<double>(screenH) / static_cast<double>(imageH);
        scale = std::min(sx, sy) * m;
        if (scale <= 0.0 || !std::isfinite(scale))
            scale = 1.0;
        offsetX = (static_cast<double>(screenW) - static_cast<double>(imageW) * scale) / 2.0;
        offsetY = (static_cast<double>(screenH) - static_cast<double>(imageH) * scale) / 2.0;
    }

    // Zoom about a fixed screen anchor (keeps the image point under `anchor`
    // stationary). Clamped to [minScale, maxScale].
    void zoomAt(double anchorX, double anchorY, double factor, double minScale = 0.05,
                double maxScale = 50.0)
    {
        if (!(factor > 0.0) || !std::isfinite(factor) || !(scale > 0.0) || !std::isfinite(scale))
            return;
        const double imgX = (anchorX - offsetX) / scale;
        const double imgY = (anchorY - offsetY) / scale;
        scale *= factor;
        if (scale < minScale)
            scale = minScale;
        if (scale > maxScale)
            scale = maxScale;
        offsetX = anchorX - imgX * scale;
        offsetY = anchorY - imgY * scale;
    }

    void pan(double dxScreen, double dyScreen)
    {
        offsetX += dxScreen;
        offsetY += dyScreen;
    }

    // Visible source-image rectangle (in full-res image pixels), clamped to the
    // image bounds [0,0,imageW,imageH]. Empty when nothing is visible.
    void visibleImageRect(int imageW, int imageH, int &x, int &y, int &w, int &h) const
    {
        x = 0;
        y = 0;
        w = 0;
        h = 0;
        if (imageW <= 0 || imageH <= 0 || screenW <= 0 || screenH <= 0)
            return;
        if (!(scale > 0.0) || !std::isfinite(scale) || !std::isfinite(offsetX) ||
            !std::isfinite(offsetY))
            return;

        const double ix0 = (0.0 - offsetX) / scale;
        const double iy0 = (0.0 - offsetY) / scale;
        const double ix1 = (static_cast<double>(screenW) - offsetX) / scale;
        const double iy1 = (static_cast<double>(screenH) - offsetY) / scale;
        int rx = static_cast<int>(ix0);
        int ry = static_cast<int>(iy0);
        int rx1 = static_cast<int>(ix1);
        int ry1 = static_cast<int>(iy1);
        if (rx < 0)
            rx = 0;
        if (ry < 0)
            ry = 0;
        if (rx1 > imageW)
            rx1 = imageW;
        if (ry1 > imageH)
            ry1 = imageH;
        // Fully off-image (viewport left/above or right/below the image) -> empty.
        if (rx >= imageW || ry >= imageH || rx1 <= rx || ry1 <= ry)
        {
            return;
        }
        x = rx;
        y = ry;
        w = rx1 - rx;
        h = ry1 - ry;
    }

    // Screen rect (widget pixels) for a source-image rectangle (image px).
    // Edges are floored from the same image-to-screen expression, so the right
    // edge of one tile is the left edge of the next (no gap, no overlap).
    void imageRectToScreen(int ix, int iy, int iw, int ih, int &sx, int &sy, int &sw, int &sh) const
    {
        sx = 0;
        sy = 0;
        sw = 0;
        sh = 0;
        if (!(scale > 0.0) || !std::isfinite(scale) || !std::isfinite(offsetX) ||
            !std::isfinite(offsetY))
            return;
        const double x0d = static_cast<double>(ix) * scale + offsetX;
        const double y0d = static_cast<double>(iy) * scale + offsetY;
        const double x1d = (static_cast<double>(ix) + static_cast<double>(iw)) * scale + offsetX;
        const double y1d = (static_cast<double>(iy) + static_cast<double>(ih)) * scale + offsetY;
        if (!std::isfinite(x0d) || !std::isfinite(y0d) || !std::isfinite(x1d) ||
            !std::isfinite(y1d))
            return;
        const int x0 = snapScreenEdge(x0d);
        const int y0 = snapScreenEdge(y0d);
        const int x1 = snapScreenEdge(x1d);
        const int y1 = snapScreenEdge(y1d);
        const int64_t dw = static_cast<int64_t>(x1) - static_cast<int64_t>(x0);
        const int64_t dh = static_cast<int64_t>(y1) - static_cast<int64_t>(y0);
        const int64_t maxInt = std::numeric_limits<int>::max();
        sx = x0;
        sy = y0;
        sw = static_cast<int>(dw < 0 ? 0 : (std::min)(dw, maxInt));
        sh = static_cast<int>(dh < 0 ? 0 : (std::min)(dh, maxInt));
    }
};
