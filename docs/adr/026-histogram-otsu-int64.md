# ADR 026 — Histogram bins and Otsu counts are 64-bit

## Status

Accepted

## Context

Compare histograms stored bin counts and `total` as `long`. On Windows `long`
is 32-bit, so a bin or a full-frame count overflows past 2^31 samples.
`suggestThreshold` also accumulated the difference histogram in 32-bit storage
and skipped bin 0. A degenerate histogram (every pixel in one bin, or zero
between-class variance) was clamped into 1..254, and an empty foreground
class could divide by zero.

ADR 025 still applies: fully transparent pixels stay omitted, and compare luma
stays the floating-point Rec.601 truncation. This decision does not replace it.

## Decision

- `mviewer::core::Histogram` bin vectors and `total` are `int64_t`.
- `suggestThreshold` counts pixels in `int64_t`. The search walks bins 0
  through 255. A class mean is divided only when that class weight is positive.
- A null or empty image returns 0. A single occupied bin, or a search whose
  between-class variance never becomes positive, returns that occupied bin
  (including 0 and 255). The result is not clamped to 1..254.
- `domain::Histogram` bins and `AnalysisEngine::ImageStats` histogram bins and
  `pixelCount` are `int64_t` for the same overflow. Display widgets may clamp
  a bin to 32-bit when the paint buffer is still `int`.

## Consequences

- A histogram of a huge image can record more than 2^32 samples in one bin on
  every platform.
- An all-equal difference map now reports that gray level. A split whose
  optimal threshold is 0 reports 0, which is a real Otsu split and not only the
  identical-image sentinel.
- Opaque-image luma values are unchanged.
