#pragma once

// M48 — the ONE authoritative EXIF orientation 1-8 coordinate contract.
//
// Every layer (SourceImage region decode, ImageViewer visible-region requests,
// Compare coordinate mapping, tests) maps between RAW source pixel space (the
// pixels as stored on disk, pre-transform) and ORIENTED / DISPLAYED pixel
// space (the geometry and pixels after QImageReader applies the EXIF
// auto-transform). QImageReader::read() returns ORIENTED pixels; its
// setClipRect() consumes RAW coordinates. Feeding oriented coordinates to a
// raw-coordinate consumer (or vice versa) silently decodes the wrong region,
// so this header is the single implementation of the mapping.
//
// `orientation` is the EXIF orientation tag (1 = identity). Pure std; no Qt.

#include "core/image/ImageBuffer.h"

namespace mviewer::core
{

// Rectangle in source pixel space.
struct SourceRect
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;

    bool isNull() const
    {
        return w <= 0 || h <= 0;
    }
};

// The coordinate space a rect / raster is expressed in.
enum class SourceCoordinateSpace
{
    Raw,     // as stored on disk, pre-EXIF-transform
    Display  // after applying the EXIF auto-transform (what the user sees)
};

// The dimensions a raw (rawW x rawH) source displays as after applying the
// EXIF orientation. Outputs into ow/oh.
inline void orientedSize(int rawW, int rawH, int orientation, int &ow, int &oh)
{
    const bool swap =
        orientation == 5 || orientation == 6 || orientation == 7 || orientation == 8;
    ow = swap ? rawH : rawW;
    oh = swap ? rawW : rawH;
}

// Map a DISPLAYED (oriented) pixel back to RAW source coordinates.
inline void displayToRaw(int ox, int oy, int rawW, int rawH, int orientation, int &rx, int &ry)
{
    switch (orientation)
    {
    case 2:
        rx = rawW - 1 - ox;
        ry = oy;
        break;
    case 3:
        rx = rawW - 1 - ox;
        ry = rawH - 1 - oy;
        break;
    case 4:
        rx = ox;
        ry = rawH - 1 - oy;
        break;
    case 5: // transpose
        rx = oy;
        ry = ox;
        break;
    case 6: // rotate 90 CW
        rx = oy;
        ry = rawH - 1 - ox;
        break;
    case 7: // transverse
        rx = rawH - 1 - oy;
        ry = rawW - 1 - ox;
        break;
    case 8: // rotate 270 CW
        rx = rawW - 1 - oy;
        ry = ox;
        break;
    default: // 1
        rx = ox;
        ry = oy;
        break;
    }
}

// Map a RAW source pixel to DISPLAYED (oriented) coordinates.
inline void rawToDisplay(int rx, int ry, int rawW, int rawH, int orientation, int &ox, int &oy)
{
    switch (orientation)
    {
    case 2:
        ox = rawW - 1 - rx;
        oy = ry;
        break;
    case 3:
        ox = rawW - 1 - rx;
        oy = rawH - 1 - ry;
        break;
    case 4:
        ox = rx;
        oy = rawH - 1 - ry;
        break;
    case 5: // transpose
        ox = ry;
        oy = rx;
        break;
    case 6: // rotate 90 CW
        ox = rawH - 1 - ry;
        oy = rx;
        break;
    case 7: // transverse
        ox = rawW - 1 - ry;
        oy = rawH - 1 - rx;
        break;
    case 8: // rotate 270 CW
        ox = ry;
        oy = rawW - 1 - rx;
        break;
    default: // 1
        ox = rx;
        oy = ry;
        break;
    }
}

// Map a DISPLAYED rect to the RAW source rect it corresponds to.
inline SourceRect orientedRectToRaw(const SourceRect &oriented, int rawW, int rawH,
                                    int orientation)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    displayToRaw(oriented.x, oriented.y, rawW, rawH, orientation, x0, y0);
    displayToRaw(oriented.x + oriented.w - 1, oriented.y + oriented.h - 1, rawW, rawH, orientation,
                 x1, y1);
    SourceRect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
    r.h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
    // Clamp to the raw source bounds.
    if (r.x + r.w > rawW)
        r.w = rawW - r.x;
    if (r.y + r.h > rawH)
        r.h = rawH - r.y;
    if (r.w < 0)
        r.w = 0;
    if (r.h < 0)
        r.h = 0;
    return r;
}

// Map a RAW rect to the DISPLAYED rect it covers.
inline SourceRect rawRectToOriented(const SourceRect &raw, int rawW, int rawH, int orientation)
{
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0;
    rawToDisplay(raw.x, raw.y, rawW, rawH, orientation, x0, y0);
    rawToDisplay(raw.x + raw.w - 1, raw.y + raw.h - 1, rawW, rawH, orientation, x1, y1);
    SourceRect r;
    r.x = x0 < x1 ? x0 : x1;
    r.y = y0 < y1 ? y0 : y1;
    r.w = (x0 < x1 ? x1 - x0 : x0 - x1) + 1;
    r.h = (y0 < y1 ? y1 - y0 : y0 - y1) + 1;
    return r;
}

} // namespace mviewer::core
