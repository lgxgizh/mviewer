#pragma once

#include <algorithm>
#include <cstdint>

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
    void fit(int imageW, int imageH, double margin = 0.95)
    {
        if (imageW <= 0 || imageH <= 0 || screenW <= 0 || screenH <= 0)
        {
            scale = 1.0;
            offsetX = 0.0;
            offsetY = 0.0;
            return;
        }
        const double sx = static_cast<double>(screenW) / imageW;
        const double sy = static_cast<double>(screenH) / imageH;
        scale = std::min(sx, sy) * margin;
        if (scale <= 0.0)
            scale = 1.0;
        offsetX = (screenW - imageW * scale) / 2.0;
        offsetY = (screenH - imageH * scale) / 2.0;
    }

    // Zoom about a fixed screen anchor (keeps the image point under `anchor`
    // stationary). Clamped to [minScale, maxScale].
    void zoomAt(double anchorX, double anchorY, double factor, double minScale = 0.05,
                double maxScale = 50.0)
    {
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
        const double ix0 = (0.0 - offsetX) / scale;
        const double iy0 = (0.0 - offsetY) / scale;
        const double ix1 = (screenW - offsetX) / scale;
        const double iy1 = (screenH - offsetY) / scale;
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
        // Fully off-image (viewport right/below the image) -> empty.
        if (rx1 <= 0 || ry1 <= 0)
        {
            x = 0;
            y = 0;
            w = 0;
            h = 0;
            return;
        }
        x = rx;
        y = ry;
        w = rx1 - rx;
        h = ry1 - ry;
    }

    // Screen rect (widget pixels) for a source-image rectangle (image px).
    void imageRectToScreen(int ix, int iy, int iw, int ih, int &sx, int &sy, int &sw, int &sh) const
    {
        sx = static_cast<int>(ix * scale + offsetX);
        sy = static_cast<int>(iy * scale + offsetY);
        sw = static_cast<int>(iw * scale);
        sh = static_cast<int>(ih * scale);
    }
};
