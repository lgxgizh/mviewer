#pragma once

#include <cmath>
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

// Brightness slider [-255, 255].
inline int clampBrightness(int value)
{
    if (value < -255)
        return -255;
    if (value > 255)
        return 255;
    return value;
}

// Contrast [0, 3], gamma [0.05, 8], WB gain [0.01, 5] (slider 1..500 / 100).
// A non-finite value falls back to the identity the slider uses.
inline float clampAdjustFloat(float value, float identity, float low, float high)
{
    if (!std::isfinite(value))
        return identity;
    if (value < low)
        return low;
    if (value > high)
        return high;
    return value;
}

inline float clampContrast(float value)
{
    return clampAdjustFloat(value, 1.0f, 0.0f, 3.0f);
}

inline float clampGamma(float value)
{
    return clampAdjustFloat(value, 1.0f, 0.05f, 8.0f);
}

inline float clampGain(float value)
{
    return clampAdjustFloat(value, 1.0f, 0.01f, 5.0f);
}

// Upper bound matches ImageViewer restored scale. The floor is the preset bound.
inline double clampSharedScale(double value)
{
    if (!std::isfinite(value))
        return 1.0;
    if (value < 0.01)
        return 0.01;
    if (value > 50.0)
        return 50.0;
    return value;
}

} // namespace mviewer::cw
