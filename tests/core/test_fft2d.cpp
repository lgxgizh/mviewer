// 2D FFT spectrum (report feature 10).
#include "core/analysis/Fft2D.h"

#include <cmath>
#include <cstdio>
#include <vector>

namespace
{
int g_pass = 0;
int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (false)

std::vector<float> constantImage(int w, int h, float value)
{
    return std::vector<float>(static_cast<size_t>(w) * static_cast<size_t>(h), value);
}

std::vector<float> sineImage(int w, int h, int periodsX)
{
    std::vector<float> out(static_cast<size_t>(w) * static_cast<size_t>(h), 0.0f);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            out[static_cast<size_t>(y) * w + x] =
                static_cast<float>(std::sin(2.0 * 3.14159265358979323846 * periodsX * x / w));
    return out;
}
} // namespace

int main()
{
    using mviewer::core::computeFftMagnitude;
    using mviewer::core::fftPowerOfTwo;
    using mviewer::core::hasOffAxisPeak;

    CHECK(fftPowerOfTwo(1) == 2 && fftPowerOfTwo(2) == 2 && fftPowerOfTwo(3) == 4 &&
              fftPowerOfTwo(1025) == 2048,
          "fftPowerOfTwo rounds up to a power of two");

    // A constant image has no structure: the magnitude is ~0 everywhere.
    const auto flat = constantImage(8, 8, 12.0f);
    const auto flatFft = computeFftMagnitude(flat, 8, 8);
    CHECK(flatFft.width == 8 && flatFft.height == 8 && flatFft.magnitude.size() == 64,
          "a constant image transforms at its own size");
    double flatMax = 0.0;
    for (const float v : flatFft.magnitude)
        flatMax = std::max(flatMax, static_cast<double>(v));
    CHECK(flatMax < 1e-9, "a constant image has a flat spectrum after DC removal");
    CHECK(!hasOffAxisPeak(flatFft, 4.0), "a flat spectrum raises no moire tripwire");

    // A pure horizontal sine concentrates energy at exactly one off-axis pair.
    const auto sine = sineImage(64, 64, 4);
    const auto sineFft = computeFftMagnitude(sine, 64, 64);
    CHECK(sineFft.width == 64 && sineFft.height == 64, "the sine image is already a power of two");
    float peak = 0.0f;
    int peakX = 0;
    int peakY = 0;
    for (int y = 0; y < sineFft.height; ++y)
        for (int x = 0; x < sineFft.width; ++x)
        {
            const float v = sineFft.magnitude[static_cast<size_t>(y) * sineFft.width + x];
            if (v > peak)
            {
                peak = v;
                peakX = x;
                peakY = y;
            }
        }
    printf("  (sine peak magnitude %.6f)\n", static_cast<double>(peak));
    CHECK(peak > 0.005f, "the sine produces a strong spectral peak");
    // Unshifted FFT: 4 cycles over 64 samples lands at bin 4 (and the mirror at 60).
    CHECK((peakX == 4 && peakY == 0) || (peakX == 60 && peakY == 0),
          "the peak sits at the expected frequency bin");
    CHECK(hasOffAxisPeak(sineFft, 4.0), "a single-frequency image trips the off-axis tripwire");
    CHECK(!hasOffAxisPeak(sineFft, 1.0e9), "an absurd threshold never trips");

    // Odd sizes are padded, not rejected.
    const auto odd = sineImage(30, 22, 3);
    const auto oddFft = computeFftMagnitude(odd, 30, 22);
    CHECK(oddFft.width == 32 && oddFft.height == 32, "odd sizes pad to the next power of two");

    // Malformed input yields an empty result rather than reading out of bounds.
    const auto bad = computeFftMagnitude(std::vector<float>(3, 0.0f), 2, 2);
    CHECK(bad.magnitude.empty(), "a size mismatch is rejected");

    // The spectrum is unshifted: DC lives in the four corners and the centre is
    // ordinary mid-band signal. The tripwire must ignore the corner DC but react
    // to a strong centre component (it used to mask the centre instead).
    mviewer::core::Fft2DResult mid;
    mid.width = 64;
    mid.height = 64;
    mid.magnitude.assign(64 * 64, 0.0f);
    mid.magnitude[32 * 64 + 32] = 100.0f;
    CHECK(hasOffAxisPeak(mid, 4.0), "a strong centre component trips the tripwire");

    mviewer::core::Fft2DResult dc;
    dc.width = 64;
    dc.height = 64;
    dc.magnitude.assign(64 * 64, 0.0f);
    dc.magnitude[0] = 100.0f;
    dc.magnitude[63 * 64 + 63] = 100.0f;
    CHECK(!hasOffAxisPeak(dc, 4.0), "a lone DC corner does not trip the tripwire");

    // Non-power-of-two sizes: the column pass must still transform every padded
    // column, because the row pass spreads each row's energy across all of them.
    // Compare against a naive DFT on the same padded plane.
    {
        const int w = 12;
        const int h = 10;
        std::vector<float> img(static_cast<size_t>(w) * static_cast<size_t>(h));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                img[static_cast<size_t>(y) * w + x] =
                    static_cast<float>(((x * 7 + y * 3) % 11) - 5);
        const auto got = computeFftMagnitude(img, w, h);
        CHECK(got.width == 16 && got.height == 16, "a 12x10 image pads to 16x16");

        double mean = 0.0;
        for (const float v : img)
            mean += v;
        mean /= static_cast<double>(img.size());

        std::vector<std::vector<double>> plane(
            static_cast<size_t>(16), std::vector<double>(16, 0.0));
        for (int y = 0; y < h; ++y)
            for (int x = 0; x < w; ++x)
                plane[static_cast<size_t>(y)][static_cast<size_t>(x)] = img[static_cast<size_t>(y) * w + x] - mean;

        double worst = 0.0;
        for (int v = 0; v < 16; ++v)
        {
            for (int u = 0; u < 16; ++u)
            {
                double re = 0.0;
                double im = 0.0;
                for (int y = 0; y < 16; ++y)
                {
                    for (int x = 0; x < 16; ++x)
                    {
                        const double phase = -2.0 * 3.14159265358979323846 *
                                              (static_cast<double>(u) * x / 16.0 +
                                               static_cast<double>(v) * y / 16.0);
                        re += plane[static_cast<size_t>(y)][static_cast<size_t>(x)] * std::cos(phase);
                        im += plane[static_cast<size_t>(y)][static_cast<size_t>(x)] * std::sin(phase);
                    }
                }
                const double expected = std::sqrt(re * re + im * im) / (16.0 * 16.0);
                const double actual =
                    got.magnitude[static_cast<size_t>(v) * got.width + static_cast<size_t>(u)];
                worst = std::max(worst, std::abs(expected - actual));
            }
        }
        printf("  (12x10 vs naive DFT, worst bin error %.3e)\n", worst);
        CHECK(worst < 1e-4, "non-power-of-two FFT matches a naive DFT on the padded plane");
    }

    printf("\n==== FFT2D test: %d passed, %d failed ====\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
