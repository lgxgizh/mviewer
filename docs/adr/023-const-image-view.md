# ADR 023 — Read-only pixel access uses ConstImageView

## Status

Accepted

## Context

`ImageData::view() const` `const_cast`s the pixel pointer and returns a
mutable `ImageBuffer`. Analyzers, histogram, and statistics only read those
bytes, but the const overload made a const image look writable and skipped
copy-on-write. `ConstImageBuffer` / `constView()` already existed; callers
on the read path were still using the const-casting overload. The compare
histogram (`src/core/compare/Histogram.h`) is one of those readers.

## Decision

- `ConstImageView` is `ConstImageBuffer`: same fields, `const uint8_t *data`.
- Read-only call sites take `constView()` / `ConstImageView`. That includes
  built-in analyzers, preview and ROI statistics, `AnalysisEngine` metrics,
  Qt image conversion, export luminance, the compare histogram, aligner
  source reads, mipmap and nearest-neighbor source reads, adjustment source
  reads, and the GPU tile upload.
- `ImageData::view() const` is removed. A const image has no mutable view.
  `ImageFrame::view() const` is removed with it; reads use `constView()`.
- Non-const `ImageData::view()` still returns a mutable alias and does not
  detach. In-place writes on a buffer that may be shared use `mutableView()`.

## Consequences

- A const image can no longer be mutated through `view()`.
- Pixel values and layouts are unchanged.
- Callers that need to write allocate their own buffer and use the non-const
  `view()` (or `mutableView()` when the buffer may be shared).
