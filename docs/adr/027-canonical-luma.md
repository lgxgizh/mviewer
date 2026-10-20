# ADR 027 — One canonical 8-bit luma

## Status

Accepted. Supersedes the luma notes in ADR 025 and ADR 026.

## Context

Four "luma" formulas coexisted for the same 8-bit display-referred pixel:

- `luminance()` in `ImageBuffer.h` (histogram, `ImageFrame`, `ImageStats`,
  `Aligner`, image overlays): `(int)(0.299 R + 0.587 G + 0.114 B)` in
  floating point. The truncation maps 65 of the 256 gray levels one LSB low
  (for example gray 1 → 0), so a gray image's luma histogram was biased.
- `lumaRec601()` in `PixelAccess.h` (AnalysisEngine ROI stats, brightness /
  contrast / exposure / blur analyzers) and the USM luma-only path:
  `(19595 R + 38470 G + 7471 B) >> 16`, also truncating.
- Pixel Inspector neighborhood stats: Rec.709 weights on gamma-encoded values,
  so its mean disagreed with the ROI stats for the same pixels.
- `(77 R + 150 G + 29 B) >> 8` for the Grayscale8 crosshair marker colour.

Noise, sharpness, entropy, dead-pixel and MTF use the plain channel mean on
purpose; that helper was named `lumaAverage`, which read like a luma.

## Decision

- `lumaRec601_8(r, g, b) = (19595 R + 38470 G + 7471 B + 32768) >> 16` in
  `ImageBuffer.h` is the only 8-bit luma. It rounds to nearest (|error| <= 0.5
  LSB against exact Rec.601) and, because the weights sum to 65536, maps every
  gray level to itself. It lives in `ImageBuffer.h` rather than `PixelAccess.h`
  because the histogram path sits below the analyzer helpers;
  `PixelAccess.h`'s `lumaRec601()` and `luminance()` delegate to it.
- The channel mean is `rgbMean()` / `rgbMeanF()` — named as what it is.
- Linear-light CIE Y (Rec.709 / sRGB primaries) in the Pixel Inspector colour
  readouts and the ColorChecker analyzer is a different quantity and stays.
- No Rec.601 / Rec.709 preference: every user-facing label already says
  BT.601, and a switch would have to be threaded through every consumer above.

## Consequences

Values that used to truncate shift by at most +1 LSB (never on gray pixels).
Tests that computed expectations through `luminance()` follow automatically;
`histogram_tests` pins the contract (gray identity, primaries, rounding, and
agreement between the histogram and analyzer paths).
