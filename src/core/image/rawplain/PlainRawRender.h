#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/rawplain/PlainRawFormat.h"

#include <cstdint>
#include <vector>

// ADR 033: unpacked samples -> RGB24 for display. Pipeline per sample:
// (s - black) / (white - black), clamp [0, 1], x white-balance gain of its CFA
// colour, x digital gain, clamp [0, 1]; then bilinear demosaic of the linear
// values (Mono: no demosaic, R = G = B); then sRGB OETF (when srgbGamma) and
// rounding to 8 bits. Qt-free, never throws.

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

struct WbGains
{
    double r = 1.0;
    double g = 1.0;
    double b = 1.0;
};

// Gains for p.wb on raw samples (size width*height, black subtracted inside):
// None -> 1,1,1. Manual -> wbGainR, 1, wbGainB. GrayWorld -> mean(G)/mean(R),
// 1, mean(G)/mean(B) over samples below 98% of white. WhitePatch -> same
// ratios using, per channel, the mean of samples at or above that channel's
// 99th percentile, excluding samples >= 98% of white. Mono -> 1,1,1. A zero
// or missing channel -> gain 1. Automatic gains clamped to [0.1, 16].
WbGains computeWhiteBalance(const Params &p, const std::vector<std::uint16_t> &samples);

// sRGB OETF on linear [0, 1] (IEC 61966-2-1): 12.92 x below 0.0031308, else
// 1.055 x^(1/2.4) - 0.055.
double srgbEncode(double linear);

// Renders samples (size width*height, p valid) to RGB24. maxEdge <= 0 or
// max(width, height) <= maxEdge -> full resolution, bilinear demosaic.
// Otherwise a fast preview: each output pixel takes the 2x2 CFA quad nearest
// to its nearest-sampled source position (R, mean of the Gs, B; Mono: the
// sample), output longest edge == maxEdge, aspect kept, each side >= 1.
// Uses per-CFA-position lookup tables, no per-pixel virtual calls.
// Empty ImageData on invalid input.
ImageData renderPlainRaw(const Params &p, const std::vector<std::uint16_t> &samples,
                         int maxEdge = 0);

// Same as renderPlainRaw with precomputed gains (tests, sample probe).
ImageData renderPlainRawWithGains(const Params &p, const std::vector<std::uint16_t> &samples,
                                  const WbGains &gains, int maxEdge = 0);

} // namespace mviewer::core::rawplain
