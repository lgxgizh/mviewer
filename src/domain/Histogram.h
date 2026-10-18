#pragma once
#include <array>
#include <cstdint>

namespace mviewer::domain
{

// Dense histogram with summary statistics
struct Histogram
{
    static constexpr int BINS = 256;
    // Bin counts are 64-bit so one bin can hold more than 2^31 samples.
    std::array<int64_t, BINS> luminance{};
    std::array<int64_t, BINS> red{};
    std::array<int64_t, BINS> green{};
    std::array<int64_t, BINS> blue{};
    std::array<int64_t, BINS> v{};

    double lumMean = 0.0;
    double vMean = 0.0;
    double rMean = 0.0, gMean = 0.0, bMean = 0.0;

    Histogram() = default;

    int64_t totalPixels() const noexcept
    {
        int64_t s = 0;
        for (int64_t val : luminance)
            s += val;
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
