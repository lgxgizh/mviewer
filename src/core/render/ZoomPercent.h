#pragma once

#include <cmath>
#include <limits>
#include <string>

namespace mviewer::core
{

// Whole-percent zoom for the status label and the on-image badge. A scale
// that is not finite and positive reads as "unknown" (-1 / empty text).
inline int zoomPercentFromScale(double scale)
{
    if (!(scale > 0.0) || !std::isfinite(scale))
        return -1;
    const double scaled = scale * 100.0 + 0.5;
    if (scaled >= static_cast<double>(std::numeric_limits<int>::max()))
        return std::numeric_limits<int>::max();
    return static_cast<int>(scaled);
}

inline std::string formatZoomPercent(int percent)
{
    if (percent < 0)
        return {};
    std::string text = std::to_string(percent);
    text.push_back('%');
    return text;
}

inline std::string formatZoomPercent(double scale)
{
    return formatZoomPercent(zoomPercentFromScale(scale));
}

} // namespace mviewer::core
