# ADR 030 — Zoom limits relative to the fit scale

## Status

Accepted

## Context

Zoom was clamped to a fixed absolute window of 0.05..50. A very large image
fitted below 0.05 jumped larger on the first zoom-out, because the clamp
pulled the scale back up to 0.05. A tiny image could not reach 50x its fit,
because 50 was an absolute ceiling rather than a multiple of the fit. The
same clamp lived in two places: `Viewport::zoomAt` in the image viewer and
`applyAnchorZoom` in compare.

## Decision

One helper, `zoomLimitsForFit`, lives in `core/render/Viewport.h`. The window
is the union of the historic absolute range 0.05..50 and 0.05x..50x the fit
scale, then kept inside the 1e-4..256 backstop. A missing or invalid fit
gives 0.05..50. `Viewport` stores `fitScale` from the last `fit()`. A scale
already outside the window (a stale fit) ignores further zoom away from the
window, so the view does not jump onto the limit.

## Consequences

100% is always reachable, because the window is never narrower than 0.05..50.
A viewport constructed without `fit()` keeps the historic 0.05..50 limits.
The explicit min/max overload of `zoomAt` stays for callers that need fixed
limits.
