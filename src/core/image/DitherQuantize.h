#pragma once

// 16-bit display quantization. QImage is forward-declared so this header does
// not include Qt (architecture gate R5). Callers already have the QImage type.

#include <cstdint>

class QImage;

namespace mviewer::core
{

// Deterministic 8×8 Bayer ordered dither, values 0..63 (each threshold once).
// The matrix is the recursive Bayer lattice, indexed by (y & 7, x & 7).
//
// quantize16To8 maps a 0..65535 sample to 8-bit. The base level is
// value / 257 (0..255, because 65535 / 257 == 255). A Bayer threshold adds
// one level when the sample sits strictly between quanta, so those values
// distribute spatially. An exact multiple of 257 — including 0 and 65535 —
// returns that 8-bit level at every coordinate, with no spatial noise.
// Never throws and never reads past the 8×8 table.
std::uint8_t quantize16To8(std::uint16_t value, int x, int y) noexcept;

// Write a same-size Format_RGB888 image for Format_RGBX64, Format_RGBA64, or
// Format_Grayscale16, using quantize16To8 per channel. Grayscale replicates
// the level into R, G, and B. Alpha is ignored.
// Returns false and leaves `out` unchanged when `src` is any other format,
// null, or the RGB888 buffer cannot be allocated. Never throws.
bool ditherQImage16ToRgb888(const QImage &src, QImage &out);

} // namespace mviewer::core
