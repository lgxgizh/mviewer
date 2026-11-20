#pragma once

// 2D FFT magnitude spectrum (report feature 10).
//
// Used to judge aliasing, moire and fixed-pattern noise: a clean capture has
// energy concentrated at the DC corner with a smooth roll-off, while moire
// shows up as isolated off-axis spikes. The transform pads to a power of two
// and the spectrum is returned unshifted so the caller decides how to present
// it.

#include <cstdint>
#include <vector>

namespace mviewer::core
{

struct Fft2DResult
{
    int width = 0;                // padded width (power of two)
    int height = 0;               // padded height (power of two)
    std::vector<float> magnitude; // width * height, row-major, unshifted
};

/// Next power of two >= value (minimum 2).
int fftPowerOfTwo(int value);

/// 2D DFT via row/column radix-2 FFT. `samples` is row-major, length
/// width*height. Returns the unshifted magnitude.
Fft2DResult computeFftMagnitude(const std::vector<float> &samples, int width, int height);

/// True when the spectrum has a meaningful off-axis peak: the largest
/// non-DC magnitude is at least `threshold` times the mean of the remaining
/// spectrum. A quick moire / periodic-noise tripwire.
bool hasOffAxisPeak(const Fft2DResult &fft, double threshold);

} // namespace mviewer::core
