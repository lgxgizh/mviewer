#pragma once

#include "domain/Selection.h"

#include <cmath>
#include <limits>

namespace mviewer::domain
{

struct PresentationPoint
{
    double x = 0.0;
    double y = 0.0;
};

struct PresentationRect
{
    double x = 0.0;
    double y = 0.0;
    double width = 0.0;
    double height = 0.0;
};

inline PresentationPoint presentationToSource(PresentationPoint point,
                                              const PresentationRect &destination, int sourceWidth,
                                              int sourceHeight) noexcept
{
    if (!(destination.width > 0.0) || !(destination.height > 0.0) || sourceWidth <= 0 ||
        sourceHeight <= 0)
    {
        const double invalid = std::numeric_limits<double>::quiet_NaN();
        return {invalid, invalid};
    }
    return {(point.x - destination.x) * sourceWidth / destination.width,
            (point.y - destination.y) * sourceHeight / destination.height};
}

inline PresentationRect selectionToPresentation(const Selection &selection,
                                                const PresentationRect &destination,
                                                int sourceWidth, int sourceHeight) noexcept
{
    if (selection.isEmpty() || sourceWidth <= 0 || sourceHeight <= 0)
        return {};
    const double scaleX = destination.width / sourceWidth;
    const double scaleY = destination.height / sourceHeight;
    return {destination.x + selection.x * scaleX, destination.y + selection.y * scaleY,
            selection.width * scaleX, selection.height * scaleY};
}

} // namespace mviewer::domain
