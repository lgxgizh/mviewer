#pragma once

#include <algorithm>
#include <cmath>
#include <vector>

namespace mviewer
{

// Pixel-accurate inspection grid. Drawn only when each source pixel occupies
// enough screen pixels that the lattice is readable (800% and above).
inline constexpr double kPixelGridMinScale = 8.0;

inline bool pixelGridVisible(double scale)
{
    return std::isfinite(scale) && scale >= kPixelGridMinScale;
}

struct PixelGridLine
{
    double x1 = 0;
    double y1 = 0;
    double x2 = 0;
    double y2 = 0;
};

// dest maps the source rectangle (srcX, srcY, srcW, srcH) into widget space.
// Only lines that intersect the visible widget rectangle are emitted.
inline std::vector<PixelGridLine> enumeratePixelGrid(double destX, double destY, double destW,
                                                     double destH, int srcX, int srcY, int srcW,
                                                     int srcH, double visX, double visY,
                                                     double visW, double visH, int maxLines = 400)
{
    std::vector<PixelGridLine> out;
    if (srcW <= 0 || srcH <= 0 || destW <= 0.0 || destH <= 0.0 || visW <= 0.0 || visH <= 0.0)
        return out;
    const double scaleX = destW / static_cast<double>(srcW);
    const double scaleY = destH / static_cast<double>(srcH);
    if (!pixelGridVisible(std::min(scaleX, scaleY)))
        return out;

    const double destR = destX + destW;
    const double destB = destY + destH;
    const double clipL = std::max(visX, destX);
    const double clipT = std::max(visY, destY);
    const double clipR = std::min(visX + visW, destR);
    const double clipB = std::min(visY + visH, destB);
    if (clipR <= clipL || clipB <= clipT)
        return out;

    const int x0 = srcX + static_cast<int>(std::floor((clipL - destX) / scaleX));
    const int x1 = srcX + static_cast<int>(std::ceil((clipR - destX) / scaleX));
    const int y0 = srcY + static_cast<int>(std::floor((clipT - destY) / scaleY));
    const int y1 = srcY + static_cast<int>(std::ceil((clipB - destY) / scaleY));
    const int xStart = std::max(srcX, x0);
    const int xEnd = std::min(srcX + srcW, x1);
    const int yStart = std::max(srcY, y0);
    const int yEnd = std::min(srcY + srcH, y1);

    const int estimate = std::max(0, xEnd - xStart + 1) + std::max(0, yEnd - yStart + 1);
    out.reserve(static_cast<size_t>(std::min(estimate, maxLines)));

    for (int x = xStart; x <= xEnd && static_cast<int>(out.size()) < maxLines; ++x)
    {
        const double wx = destX + static_cast<double>(x - srcX) * scaleX;
        if (wx < clipL - 0.5 || wx > clipR + 0.5)
            continue;
        out.push_back(PixelGridLine{wx, clipT, wx, clipB});
    }
    for (int y = yStart; y <= yEnd && static_cast<int>(out.size()) < maxLines; ++y)
    {
        const double wy = destY + static_cast<double>(y - srcY) * scaleY;
        if (wy < clipT - 0.5 || wy > clipB + 0.5)
            continue;
        out.push_back(PixelGridLine{clipL, wy, clipR, wy});
    }
    return out;
}

} // namespace mviewer
