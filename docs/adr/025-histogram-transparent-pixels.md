# ADR 025 — Histogram samples skip fully transparent pixels

## Status

Accepted

## Context

`mviewer::core::Histogram`, `ImageFrame::computeHistogram`, `AnalysisEngine`,
and `ImageStats` all read RGBA32 / BGRA32. Analysis and image stats already
ignore a pixel whose alpha byte is 0, and they never treat that byte as a
colour sample. The compare histogram and `ImageFrame` counted those pixels,
and `ImageFrame` left `domain::Histogram::v` / `vMean` at zero.

Luma stays the existing Rec.601 truncation (`0.299 R + 0.587 G + 0.114 B`).
AnalysisEngine's integer luma is a different formula and is not changed here.

## Decision

- RGBA32 and BGRA32 histograms read only the three colour bytes. Alpha is not
  a bin.
- A pixel with alpha 0 is omitted from every channel, from `total`, and from
  means. Partial alpha is counted at its stored RGB value.
- V is `max(R, G, B)`. Grayscale V is the gray sample.
- Means divide by the number of counted samples. An all-transparent image
  keeps zero means and is still marked computed. An empty image does not.

## Consequences

- Compare histograms and `ImageFrame` statistics match the analysis panel on
  fully transparent pixels.
- Opaque images are unchanged. Callers that previously averaged transparent
  pixels into the mean now see only the visible samples.
