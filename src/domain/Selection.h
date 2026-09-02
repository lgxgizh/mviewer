#pragma once

#include <algorithm>
#include <cmath>
#include <limits>

namespace mviewer::domain
{

// Region of Interest (rectangle in image pixel coordinates)
struct Selection
{
    int x = 0, y = 0;
    int width = 0, height = 0;

    bool isEmpty() const noexcept
    {
        return width <= 0 || height <= 0;
    }
    bool contains(int px, int py) const noexcept
    {
        return px >= x && px < x + width && py >= y && py < y + height;
    }
    int area() const noexcept
    {
        return width * height;
    }
};

// Canonical half-open ROI normalization. Coordinates are source-image pixels
// and the resulting rectangle is clipped to [0,width) x [0,height). The
// helper is deliberately Qt-free so every UI and analysis path shares the
// same edge and reverse-drag semantics.
inline Selection normalizeSelection(int x0, int y0, int x1, int y1, int imageWidth,
                                    int imageHeight) noexcept
{
    const long long left = std::min<long long>(x0, x1);
    const long long top = std::min<long long>(y0, y1);
    const long long right = std::max<long long>(x0, x1);
    const long long bottom = std::max<long long>(y0, y1);
    const long long width = std::max(0LL, static_cast<long long>(imageWidth));
    const long long height = std::max(0LL, static_cast<long long>(imageHeight));
    const long long cx0 = std::clamp(left, 0LL, width);
    const long long cy0 = std::clamp(top, 0LL, height);
    const long long cx1 = std::clamp(right, 0LL, width);
    const long long cy1 = std::clamp(bottom, 0LL, height);
    Selection out;
    out.x = static_cast<int>(cx0);
    out.y = static_cast<int>(cy0);
    out.width = static_cast<int>(std::max(0LL, cx1 - cx0));
    out.height = static_cast<int>(std::max(0LL, cy1 - cy0));
    return out;
}

// Convert floating-point pointer coordinates to the canonical half-open pixel
// rectangle. floor/ceil means a drag touching any part of a pixel includes it,
// while a click with no extent remains a zero-area Selection.
inline Selection normalizeSelection(double x0, double y0, double x1, double y1, int imageWidth,
                                    int imageHeight) noexcept
{
    const auto floorToInt = [](double value) -> int
    {
        if (!std::isfinite(value))
            return value < 0.0 ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
        const double floored = std::floor(value);
        if (floored <= static_cast<double>(std::numeric_limits<int>::min()))
            return std::numeric_limits<int>::min();
        if (floored >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(floored);
    };
    const auto ceilToInt = [](double value) -> int
    {
        if (!std::isfinite(value))
            return value < 0.0 ? std::numeric_limits<int>::min() : std::numeric_limits<int>::max();
        const double ceiled = std::ceil(value);
        if (ceiled <= static_cast<double>(std::numeric_limits<int>::min()))
            return std::numeric_limits<int>::min();
        if (ceiled >= static_cast<double>(std::numeric_limits<int>::max()))
            return std::numeric_limits<int>::max();
        return static_cast<int>(ceiled);
    };
    const double left = std::min(x0, x1);
    const double top = std::min(y0, y1);
    const double right = std::max(x0, x1);
    const double bottom = std::max(y0, y1);
    return normalizeSelection(floorToInt(left), floorToInt(top), ceilToInt(right),
                              ceilToInt(bottom), imageWidth, imageHeight);
}

} // namespace mviewer::domain
