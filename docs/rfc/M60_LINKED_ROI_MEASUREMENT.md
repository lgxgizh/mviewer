# M60 RFC — Linked ROI Measurement & RGB Ratio Analysis

Status: Implemented in the `1.0.17` patch line (2026-09-02)

## Scope

M60 extends the existing `Selection`/`CompareEngine` ROI path into a direct
Compare measurement workflow. A right-button drag in an ordinary Compare grid
produces one canonical source-coordinate rectangle. Equal-dimension panes
mirror that rectangle immediately, and the existing Analysis panel displays
per-pane source RGB means, ratios, pixel counts, and the two-image delta.

The implementation deliberately does not create a second ROI model. The
`Selection` value remains the single geometry, `CompareWorkspace` owns the
linked-state policy, and `computeROIChannelStats` is the pure core measurement
path consumed by `RGBMeanAnalyzer` and the asynchronous UI batch.

## Coordinate and dimension contract

Selections use half-open source pixels, `[x,x+w) × [y,y+h)`. A shared
`normalizeSelection` helper handles reverse drags, floating pointer edges,
outside drags, clipping, one-pixel regions, and zero-area selections. Linked
ROI is enabled only when every participating source has identical dimensions.
Different-size panes never receive a proportional mapping; a local active-pane
rectangle may remain useful, while the panel reports
`Linked ROI unavailable — image dimensions differ` and no linked statistics are
published.

## Measurement contract

`ROI Measurement — Source RGB` reports R/G/B means to two decimals, ratios to
four decimals, and 64-bit pixel counts. Ratios are channel-mean ratios
(`ΣR/ΣG` and `ΣB/ΣG`), not an average of per-pixel ratios. A zero green sum
marks both ratios unavailable and the UI renders `—`. RGB/BGR/RGBA/BGRA and
grayscale storage are decoded in source order; alpha is ignored.

Compare adjustments and monitor/source ICC presentation conversion are not part
of the baseline. Measurements use immutable source/analysis pixels. A
source-backed pane uses `SourceImage::decodeRegion` when available, avoiding a
full large-image materialization for a small ROI; a decoder that cannot provide
that path is not silently represented as source-accurate. The current
`ImageData` analysis boundary is 8-bit, so high-bit precision remains an
explicit limitation rather than an implied guarantee.

## Async and lifecycle behavior

Geometry updates are cheap and live during drag. Statistics are submitted to
the Analysis scheduler as value-owned batches with a monotonically increasing
generation. New ROI, pair navigation, clear, or workspace destruction cancels
or invalidates older work; queued delivery checks generation, pane count,
geometry, and linked-state before updating the table. Mouse release always
submits the final exact rectangle. Escape and the panel Clear ROI button remove
all overlays, clear ROI histogram scope, and invalidate pending results.

The one canonical rectangle is reapplied after zoom, pan, fit, resize,
fullscreen, layout/mode rebuilds, and CompareSession restore. Session data
stores geometry, not numerical results; restored sources trigger fresh
measurement. N-image equal-dimension comparisons share the rectangle and get a
row per pane; B−A deltas are shown only for exactly two panes.

## Verification

`m60_linked_roi_tests` covers geometry, all supported pixel layouts, ratios,
zero-green handling, analyzer parity, and a multi-million-pixel region.
Compare acceptance covers mode persistence and histogram consistency. The
source-stable full gate and GitHub packaging result are recorded in
`docs/review/M60_LINKED_ROI_CLOSURE_2026-09-02.md`.
