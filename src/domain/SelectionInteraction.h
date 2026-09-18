#pragma once

#include "domain/Selection.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>

namespace mviewer::domain
{

enum class SelectionHandle
{
    None,
    Create,
    Move,
    Left,
    Right,
    Top,
    Bottom,
    TopLeft,
    TopRight,
    BottomLeft,
    BottomRight
};

namespace
{
// Bit flags for ROI handle hit-testing. Priority table below matches the
// historical corner > edge > interior order (first match wins).
constexpr std::uint8_t kHitNearLeft = 1u << 0;
constexpr std::uint8_t kHitNearRight = 1u << 1;
constexpr std::uint8_t kHitNearTop = 1u << 2;
constexpr std::uint8_t kHitNearBottom = 1u << 3;
constexpr std::uint8_t kHitWithinX = 1u << 4;
constexpr std::uint8_t kHitWithinY = 1u << 5;
constexpr std::uint8_t kHitInterior = 1u << 6;

struct HitRule
{
    std::uint8_t required = 0;
    std::uint8_t mask = 0;
    SelectionHandle handle = SelectionHandle::None;
};

constexpr std::array<HitRule, 9> kHitRules{{
    {static_cast<std::uint8_t>(kHitNearLeft | kHitNearTop),
     static_cast<std::uint8_t>(kHitNearLeft | kHitNearTop), SelectionHandle::TopLeft},
    {static_cast<std::uint8_t>(kHitNearRight | kHitNearTop),
     static_cast<std::uint8_t>(kHitNearRight | kHitNearTop), SelectionHandle::TopRight},
    {static_cast<std::uint8_t>(kHitNearLeft | kHitNearBottom),
     static_cast<std::uint8_t>(kHitNearLeft | kHitNearBottom), SelectionHandle::BottomLeft},
    {static_cast<std::uint8_t>(kHitNearRight | kHitNearBottom),
     static_cast<std::uint8_t>(kHitNearRight | kHitNearBottom), SelectionHandle::BottomRight},
    {static_cast<std::uint8_t>(kHitNearLeft | kHitWithinY),
     static_cast<std::uint8_t>(kHitNearLeft | kHitWithinY), SelectionHandle::Left},
    {static_cast<std::uint8_t>(kHitNearRight | kHitWithinY),
     static_cast<std::uint8_t>(kHitNearRight | kHitWithinY), SelectionHandle::Right},
    {static_cast<std::uint8_t>(kHitNearTop | kHitWithinX),
     static_cast<std::uint8_t>(kHitNearTop | kHitWithinX), SelectionHandle::Top},
    {static_cast<std::uint8_t>(kHitNearBottom | kHitWithinX),
     static_cast<std::uint8_t>(kHitNearBottom | kHitWithinX), SelectionHandle::Bottom},
    {kHitInterior, kHitInterior, SelectionHandle::Move},
}};

inline std::uint8_t buildHitTestFlags(const Selection &selection, double x, double y,
                                      double toleranceX, double toleranceY) noexcept
{
    const double left = selection.x;
    const double top = selection.y;
    const double right = selection.x + selection.width;
    const double bottom = selection.y + selection.height;

    std::uint8_t flags = 0;
    if (std::abs(x - left) <= toleranceX)
        flags = static_cast<std::uint8_t>(flags | kHitNearLeft);
    if (std::abs(x - right) <= toleranceX)
        flags = static_cast<std::uint8_t>(flags | kHitNearRight);
    if (std::abs(y - top) <= toleranceY)
        flags = static_cast<std::uint8_t>(flags | kHitNearTop);
    if (std::abs(y - bottom) <= toleranceY)
        flags = static_cast<std::uint8_t>(flags | kHitNearBottom);
    if (x >= left - toleranceX)
    {
        if (x <= right + toleranceX)
            flags = static_cast<std::uint8_t>(flags | kHitWithinX);
    }
    if (y >= top - toleranceY)
    {
        if (y <= bottom + toleranceY)
            flags = static_cast<std::uint8_t>(flags | kHitWithinY);
    }
    if (x >= left)
    {
        if (x <= right)
        {
            if (y >= top)
            {
                if (y <= bottom)
                    flags = static_cast<std::uint8_t>(flags | kHitInterior);
            }
        }
    }
    return flags;
}
} // namespace

inline SelectionHandle hitTestSelection(const Selection &selection, double x, double y,
                                        double toleranceX, double toleranceY) noexcept
{
    if (selection.isEmpty())
        return SelectionHandle::None;
    if (!std::isfinite(x))
        return SelectionHandle::None;
    if (!std::isfinite(y))
        return SelectionHandle::None;

    const std::uint8_t flags = buildHitTestFlags(selection, x, y, toleranceX, toleranceY);
    for (const HitRule &rule : kHitRules)
    {
        if ((flags & rule.mask) == rule.required)
            return rule.handle;
    }
    return SelectionHandle::None;
}

inline Selection updateSelectionInteraction(const Selection &origin, SelectionHandle handle,
                                            double startX, double startY, double currentX,
                                            double currentY, int imageWidth,
                                            int imageHeight) noexcept
{
    if (handle == SelectionHandle::Create || origin.isEmpty())
        return normalizeSelection(startX, startY, currentX, currentY, imageWidth, imageHeight);
    if (handle == SelectionHandle::Move)
    {
        const int dx = static_cast<int>(std::lround(currentX - startX));
        const int dy = static_cast<int>(std::lround(currentY - startY));
        Selection moved = origin;
        moved.x = std::clamp(origin.x + dx, 0, std::max(0, imageWidth - origin.width));
        moved.y = std::clamp(origin.y + dy, 0, std::max(0, imageHeight - origin.height));
        return moved;
    }

    double left = origin.x;
    double top = origin.y;
    double right = origin.x + origin.width;
    double bottom = origin.y + origin.height;
    switch (handle)
    {
    case SelectionHandle::Left:
        left = currentX;
        break;
    case SelectionHandle::Right:
        right = currentX;
        break;
    case SelectionHandle::Top:
        top = currentY;
        break;
    case SelectionHandle::Bottom:
        bottom = currentY;
        break;
    case SelectionHandle::TopLeft:
        left = currentX;
        top = currentY;
        break;
    case SelectionHandle::TopRight:
        right = currentX;
        top = currentY;
        break;
    case SelectionHandle::BottomLeft:
        left = currentX;
        bottom = currentY;
        break;
    case SelectionHandle::BottomRight:
        right = currentX;
        bottom = currentY;
        break;
    default:
        return origin;
    }
    const Selection resized = normalizeSelection(left, top, right, bottom, imageWidth, imageHeight);
    // A persisted edit is always positive. Crossing an edge is supported by
    // normalization; the exact zero-width/height crossing point retains the
    // last valid geometry instead of publishing an invalid selection.
    return resized.isEmpty() ? origin : resized;
}

} // namespace mviewer::domain
