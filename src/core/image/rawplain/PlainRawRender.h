#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/rawplain/PlainRawFormat.h"

#include <cstdint>
#include <functional>
#include <vector>

// ADR 033: unpacked samples -> display raster. Pipeline per sample:
// (s - black) / (white - black), clamp [0, 1], x white-balance gain of its CFA
// colour (gains are scaled so the smallest is 1), x digital gain, clamp [0, 1];
// then bilinear demosaic of the linear values (Mono: no demosaic);
// then sRGB OETF (when srgbGamma) and rounding to 8 bits. Colour CFA output
// is RGB24. Mono output is Grayscale8, one byte holding the code a colour
// path would have stored in R, G, and B. Qt-free, never throws.

namespace mviewer::core::rawplain
{

enum class CfaColor : std::uint8_t
{
    Red,
    Green,
    Blue
};

// Colour of the sample at (x, y); Mono -> Green.
CfaColor cfaColorAt(Cfa cfa, int x, int y);

// Per-channel multipliers. Every mode returns min(r, g, b) == 1, so a sample
// at or above white stays 1 in every channel after the post-gain clamp and
// clipped highlights stay neutral.
struct WbGains
{
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
};

// Gains for p.wb on raw samples (size width*height, black subtracted inside).
// None and Mono -> 1,1,1. Manual -> wbGainR, 1, wbGainB, each divided by
// min(wbGainR, 1, wbGainB). GrayWorld -> mean(G)/mean(R), 1, mean(G)/mean(B)
// over samples below 98% of white. WhitePatch -> the same ratios from, per
// channel, the mean of samples at or above that channel's 99th percentile,
// still excluding samples >= 98% of white. Automatic ratios are divided by
// their minimum, then each gain is clamped to [1, 16]. A zero or missing
// channel contributes ratio 1.
WbGains computeWhiteBalance(const Params &p, const std::vector<std::uint16_t> &samples);

// sRGB OETF on linear [0, 1] (IEC 61966-2-1): 12.92 x below 0.0031308, else
// 1.055 x^(1/2.4) - 0.055.
double srgbEncode(double linear);

// Renders samples (size width*height, p valid). Colour CFAs land in RGB24.
// Mono lands in Grayscale8 (one byte, the 8-bit code R=G=B used to share).
// maxEdge <= 0 or max(width, height) <= maxEdge -> full resolution, bilinear
// demosaic (Mono copies the sample). Otherwise a fast preview: each output
// pixel averages up to 4x4 CFA quads (up to 8x8 samples) evenly spread over
// its source footprint, per channel after the linear LUT (fewer quads when
// the footprint is smaller; Mono averages the samples). Output longest edge
// == maxEdge, aspect kept, each side >= 1. Uses per-CFA-position lookup
// tables, no per-pixel virtual calls. Empty ImageData on invalid input.
ImageData renderPlainRaw(const Params &p, const std::vector<std::uint16_t> &samples,
                         int maxEdge = 0);

// Same as renderPlainRaw with precomputed gains (tests, sample probe).
ImageData renderPlainRawWithGains(const Params &p, const std::vector<std::uint16_t> &samples,
                                  const WbGains &gains, int maxEdge = 0);

// Same pixels as renderPlainRaw. Takes ownership of samples and linearises
// them in place, so a full render does not allocate a second frame.
ImageData renderPlainRawOwned(const Params &p, std::vector<std::uint16_t> &&samples,
                              int maxEdge = 0);

// fn runs on row bands [y0, y1) that cover [0, rowCount) and has returned for
// every band before the runner returns. nullptr (the default, and what
// setPlainRawRowsRunner(nullptr) restores) calls fn(0, rowCount) on this thread.
// The rawplain module stays std-only; the decoder installs a thread pool.
using PlainRawRowsRunner = void (*)(int rowCount, const std::function<void(int, int)> &fn);
void setPlainRawRowsRunner(PlainRawRowsRunner runner) noexcept;

} // namespace mviewer::core::rawplain
