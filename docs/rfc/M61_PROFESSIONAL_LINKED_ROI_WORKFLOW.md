# M61 RFC — Professional linked ROI workflow convergence

Status: Implemented in the `1.0.18` patch line (2026-09-02)

## Product contract

M61 completes the M60 ROI feature as a coherent Compare workflow. A single
`mviewer::domain::Selection` is the canonical half-open source rectangle in
Grid, Split, Overlay, Swipe, and Checkerboard. The user creates it with a right
drag, moves it by dragging inside, and resizes it from four edges or four
corners. Mode changes, zoom, pan, fit, resize, and fullscreen never create a
second geometry or round-trip through presentation pixels.

`SelectionInteraction.h` owns Qt-free hit testing and clipped create/move/resize
semantics. `SelectionMapping.h` owns source↔presentation mapping. Grid and Canvas
consume those same helpers, while `roioverlay.h` supplies the shared high-
contrast overlay, label, and handles. Canvas caches the pixel-composited base
surface separately from annotation painting, so pointer movement repaints only
the ROI and submits no statistics until release.

## Measurement lifecycle

The UI exposes `Idle`, `Measuring`, `Ready`, `Unsupported`, `Failed`, and
`Backpressured`. A value-owned Analysis task checks cancellation before each
pane and at every source scan row. Each result carries generation, selection,
linked state, and pane count; delivery is accepted only if all still match.
Clear, navigation, a newer ROI, or destruction cancels/invalidates old work.
A rejected scheduler submission immediately becomes `Backpressured` and can be
retried by the next committed ROI.

The full table has stable image/mean/ratio/pixel/status columns, filename
tooltips, right-aligned numeric values, and TSV clipboard export. When Analysis
is hidden, a non-modal clickable HUD continues to display lifecycle, geometry,
and current results; it changes corner when it would cover the ROI center.

## Source and color truth

ROI measurement is source RGB, not the ICC-converted display raster and not the
Compare-adjusted presentation. The current `ImageData` boundary is 8-bit, so
the UI says `Source RGB · 8-bit analysis`; M61 does not claim native 16-bit
statistics.

`ISourceImageRegionTruth` is an additive decoder capability that distinguishes
an evidence-backed bounded source-pixel region from a path that may materialize
the full raster. Current declarations are:

| Source path | Region truth | ROI measurement |
| --- | --- | --- |
| Qt JPEG | bounded source pixels | supported |
| Windows unrotated WIC TIFF | bounded source pixels | supported |
| PNG / BMP / optional Qt plugins | may materialize full raster | unsupported |
| Missing/corrupt/unclaimed source | no valid source region | failed/unsupported |

This classification is queried before decoding, so an unsupported format does
not first materialize pixels and only then get rejected. It is deliberately
stricter than display suitability: a decoder may still produce a useful
viewport representation without qualifying for exact, bounded source analysis.

## Verification contract

`m61_roi_workflow_tests` drives production widgets with real right-button
events and covers all presentation modes, live mirroring, final commit,
move/resize, mapping stability, hidden-panel HUD, clipboard, cancellation,
backpressure, unequal dimensions, source-format truth, and annotation-cache
reuse. `workflow_ux_tests` adds the real Split-mode gesture to the canonical
Compare workflow. `m61_roi_benchmark` records full 24/60/100 MP scan cost and
row-cancellation exit latency. Full evidence and remaining manual boundaries
are in the M61 closure review.
