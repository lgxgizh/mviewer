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
// `scratch` lets a caller reuse one buffer across the many 1-D transforms that
// make up a 2-D FFT instead of allocating per call.
void fftRadix2(std::vector<Complex> &data, bool inverse, std::vector<Complex> *scratch)
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
    std::vector<Complex> owned;
    std::vector<Complex> &twiddle = scratch != nullptr ? *scratch : owned;
    twiddle.clear();
    for (int length = 2; length <= n; length <<= 1)
    {
        const double angle =
            2.0 * 3.14159265358979323846 / static_cast<double>(length) * (inverse ? 1.0 : -1.0);
        const Complex wlen(std::cos(angle), std::sin(angle));
        const int half = length / 2;
        // Stage twiddles are precomputed once instead of being rebuilt by a
        // complex multiply inside every butterfly: the inner loop becomes a
        // table read, and the repeated w *= wlen can no longer accumulate
        // rounding error across a stage.
        twiddle.resize(half);
        twiddle[0] = Complex(1.0, 0.0);
        for (int k = 1; k < half; ++k)
            twiddle[k] = twiddle[k - 1] * wlen;
        for (int i = 0; i < n; i += length)
            for (int k = 0; k < half; ++k)
            {
                const Complex u = data[i + k];
                const Complex v = data[i + k + half] * twiddle[k];
                data[i + k] = u + v;
                data[i + k + half] = u - v;
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

    // Rows. Rows at or beyond `height` are still all-zero padding (the plane was
    // value-initialised), so their transform is identically zero - skip them.
    std::vector<Complex> row(static_cast<size_t>(paddedW));
    std::vector<Complex> twiddle;
    for (int y = 0; y < height; ++y)
    {
        for (int x = 0; x < paddedW; ++x)
            row[static_cast<size_t>(x)] = plane[static_cast<size_t>(y) * paddedW + x];
        fftRadix2(row, false, &twiddle);
        for (int x = 0; x < paddedW; ++x)
            plane[static_cast<size_t>(y) * paddedW + x] = row[static_cast<size_t>(x)];
    }
    // Columns. Every padded column carries energy after the row pass (the row
    // FFT spreads each row across all paddedW columns), so all of them must be
    // transformed - including x in [width, paddedW). Only the padding *rows*
    // (y >= height) stay zero, which the radix-2 transform handles as-is.
    std::vector<Complex> column(static_cast<size_t>(paddedH));
    for (int x = 0; x < paddedW; ++x)
    {
        for (int y = 0; y < paddedH; ++y)
            column[static_cast<size_t>(y)] = plane[static_cast<size_t>(y) * paddedW + x];
        fftRadix2(column, false, &twiddle);
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
    // The FFT output is unshifted: DC sits in the four CORNERS, and the centre
    // is mid-band energy. Masking the centre (as this used to) discarded real
    // signal and left the DC term in the comparison.
    auto nearDc = [w = fft.width, h = fft.height](int x, int y)
    {
        const int mx = std::min(x, w - x);
        const int my = std::min(y, h - y);
        return mx <= 1 && my <= 1;
    };
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
