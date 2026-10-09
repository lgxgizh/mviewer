#pragma once

#include <cstdint>

namespace mviewer::cw
{

// Snap to {0, 90, 180, 270}. Negatives are wrapped into [0, 360) first.
// A midpoint (45, 135, 225, 315) snaps toward the lower angle, so 45 becomes 0.
inline int clampRotation(int degrees)
{
    int normalized = degrees % 360;
    if (normalized < 0)
        normalized += 360;
    const int quadrant = normalized / 90;
    const int remainder = normalized % 90;
    if (remainder <= 45)
        return quadrant * 90;
    const int next = quadrant + 1;
    return next >= 4 ? 0 : next * 90;
}

inline uint8_t clampThreshold(int value)
{
    if (value < 0)
        return 0;
    if (value > 255)
        return 255;
    return static_cast<uint8_t>(value);
}

inline int clampBlinkMs(int value)
{
    if (value < 50)
        return 50;
    if (value > 5000)
        return 5000;
    return value;
}

inline int clampColumns(int value)
{
    if (value < 1)
        return 1;
    if (value > 8)
        return 8;
    return value;
}

struct PresetCrop
{
    int x = 0;
    int y = 0;
    int w = 0;
    int h = 0;
};

inline int nonNegative(int value)
{
    return value < 0 ? 0 : value;
}

inline PresetCrop clampCrop(int x, int y, int w, int h)
{
    PresetCrop crop;
    crop.x = nonNegative(x);
    crop.y = nonNegative(y);
    crop.w = nonNegative(w);
    crop.h = nonNegative(h);
    return crop;
}

} // namespace mviewer::cw
