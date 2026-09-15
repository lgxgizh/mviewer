#pragma once
#include <array>
#include <cstdint>

namespace mviewer::domain
{

// Dense histogram with summary statistics
struct Histogram
{
    static constexpr int BINS = 256;
    std::array<int, BINS> luminance{};
    std::array<int, BINS> red{};
    std::array<int, BINS> green{};
    std::array<int, BINS> blue{};
    std::array<int, BINS> v{};

    double lumMean = 0.0;
    double vMean = 0.0;
    double rMean = 0.0, gMean = 0.0, bMean = 0.0;

    Histogram() = default;

    int64_t totalPixels() const noexcept
    {
        int64_t s = 0;
        for (int val : luminance)
            s += static_cast<int64_t>(val);
        return s;
    }

    void clear() noexcept
    {
        luminance.fill(0);
        red.fill(0);
        green.fill(0);
        blue.fill(0);
        v.fill(0);
        lumMean = vMean = rMean = gMean = bMean = 0.0;
    }
};

} // namespace mviewer::domain
