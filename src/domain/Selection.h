#pragma once

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

} // namespace mviewer::domain
