#pragma once

#include "domain/Selection.h"

#include <algorithm>
#include <cmath>

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

inline SelectionHandle hitTestSelection(const Selection &selection, double x, double y,
                                        double toleranceX, double toleranceY) noexcept
{
    if (selection.isEmpty() || !std::isfinite(x) || !std::isfinite(y))
        return SelectionHandle::None;
    const double left = selection.x;
    const double top = selection.y;
    const double right = selection.x + selection.width;
    const double bottom = selection.y + selection.height;
    const bool nearLeft = std::abs(x - left) <= toleranceX;
    const bool nearRight = std::abs(x - right) <= toleranceX;
    const bool nearTop = std::abs(y - top) <= toleranceY;
    const bool nearBottom = std::abs(y - bottom) <= toleranceY;
    const bool withinX = x >= left - toleranceX && x <= right + toleranceX;
    const bool withinY = y >= top - toleranceY && y <= bottom + toleranceY;
    if (nearLeft && nearTop)
        return SelectionHandle::TopLeft;
    if (nearRight && nearTop)
        return SelectionHandle::TopRight;
    if (nearLeft && nearBottom)
        return SelectionHandle::BottomLeft;
    if (nearRight && nearBottom)
        return SelectionHandle::BottomRight;
    if (nearLeft && withinY)
        return SelectionHandle::Left;
    if (nearRight && withinY)
        return SelectionHandle::Right;
    if (nearTop && withinX)
        return SelectionHandle::Top;
    if (nearBottom && withinX)
        return SelectionHandle::Bottom;
    if (x >= left && x <= right && y >= top && y <= bottom)
        return SelectionHandle::Move;
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
