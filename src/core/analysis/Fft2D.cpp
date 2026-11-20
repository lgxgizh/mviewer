#include "core/analysis/Fft2D.h"

#include <algorithm>
#include <cmath>
#include <complex>

namespace mviewer::core
{
namespace
{
using Complex = std::complex<double>;

// Iterative radix-2 Cooley-Tukey, in place. `n` must be a power of two.
void fftRadix2(std::vector<Complex> &data, bool inverse)
{
    const int n = static_cast<int>(data.size());
    if (n < 2)
        return;
    // Bit-reversal permutation.
    for (int i = 1, j = 0; i < n; ++i)
    {
        int bit = n >> 1;
        for (; j & bit; bit >>= 1)
            j ^= bit;
        j ^= bit;
        if (i < j)
            std::swap(data[i], data[j]);
    }
    for (int length = 2; length <= n; length <<= 1)
    {
        const double angle =
            2.0 * 3.14159265358979323846 / static_cast<double>(length) * (inverse ? 1.0 : -1.0);
        const Complex wlen(std::cos(angle), std::sin(angle));
        for (int i = 0; i < n; i += length)
        {
            Complex w(1.0, 0.0);
            for (int k = 0; k < length / 2; ++k)
            {
                const Complex u = data[i + k];
                const Complex v = data[i + k + length / 2] * w;
                data[i + k] = u + v;
                data[i + k + length / 2] = u - v;
                w *= wlen;
            }
        }
    }
    if (inverse)
        for (Complex &c : data)
            c /= static_cast<double>(n);
}

float lumaSample(const std::vector<float> &samples, int width, int x, int y)
{
    return samples[static_cast<size_t>(y) * static_cast<size_t>(width) + static_cast<size_t>(x)];
}
} // namespace

int fftPowerOfTwo(int value)
{
    int power = 2;
    while (power < value)
        power <<= 1;
    return power;
}

Fft2DResult computeFftMagnitude(const std::vector<float> &samples, int width, int height)
{
    Fft2DResult result;
    if (width <= 0 || height <= 0 ||
        static_cast<size_t>(width) * static_cast<size_t>(height) != samples.size())
        return result;

    const int paddedW = fftPowerOfTwo(width);
    const int paddedH = fftPowerOfTwo(height);
    const size_t count = static_cast<size_t>(paddedW) * static_cast<size_t>(paddedH);
    std::vector<Complex> plane(count);

    // Remove the mean first: DC otherwise dwarfs everything and the log view
    // saturates.
    double mean = 0.0;
    for (const float v : samples)
        mean += v;
    mean /= static_cast<double>(samples.size());

    for (int y = 0; y < height; ++y)
        for (int x = 0; x < width; ++x)
            plane[static_cast<size_t>(y) * paddedW + x] =
                Complex(lumaSample(samples, width, x, y) - mean, 0.0);

    // Rows.
    std::vector<Complex> row(static_cast<size_t>(paddedW));
    for (int y = 0; y < paddedH; ++y)
    {
        for (int x = 0; x < paddedW; ++x)
            row[static_cast<size_t>(x)] = plane[static_cast<size_t>(y) * paddedW + x];
        fftRadix2(row, false);
        for (int x = 0; x < paddedW; ++x)
            plane[static_cast<size_t>(y) * paddedW + x] = row[static_cast<size_t>(x)];
    }
    // Columns.
    std::vector<Complex> column(static_cast<size_t>(paddedH));
    for (int x = 0; x < paddedW; ++x)
    {
        for (int y = 0; y < paddedH; ++y)
            column[static_cast<size_t>(y)] = plane[static_cast<size_t>(y) * paddedW + x];
        fftRadix2(column, false);
        for (int y = 0; y < paddedH; ++y)
            plane[static_cast<size_t>(y) * paddedW + x] = column[static_cast<size_t>(y)];
    }

    result.width = paddedW;
    result.height = paddedH;
    result.magnitude.assign(count, 0.0f);
    const double scale = 1.0 / static_cast<double>(paddedW) / static_cast<double>(paddedH);
    for (size_t i = 0; i < count; ++i)
        result.magnitude[i] = static_cast<float>(std::abs(plane[i]) * scale);
    return result;
}

bool hasOffAxisPeak(const Fft2DResult &fft, double threshold)
{
    if (fft.magnitude.empty() || !(threshold > 1.0))
        return false;
    // Skip a small DC neighbourhood: the low-frequency roll-off is normal.
    const int cx = fft.width / 2;
    const int cy = fft.height / 2;
    auto nearDc = [&](int x, int y) { return std::abs(x - cx) <= 1 && std::abs(y - cy) <= 1; };
    double sum = 0.0;
    int count = 0;
    float peak = 0.0f;
    int peakX = -1;
    int peakY = -1;
    for (int y = 0; y < fft.height; ++y)
    {
        for (int x = 0; x < fft.width; ++x)
        {
            if (nearDc(x, y))
                continue;
            const float v = fft.magnitude[static_cast<size_t>(y) * fft.width + x];
            sum += v;
            ++count;
            if (v > peak)
            {
                peak = v;
                peakX = x;
                peakY = y;
            }
        }
    }
    if (count == 0 || peakX < 0)
        return false;
    const double mean = sum / static_cast<double>(count);
    return mean > 0.0 && peak >= mean * threshold;
}

} // namespace mviewer::core
