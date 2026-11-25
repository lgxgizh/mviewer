# ADR 038 — Compare canvas GPU compositing

Date: 2026-11-24
Status: Accepted

## Context

`CompareWorkspace::paintCompareCanvas()` composited by building a full physical-
resolution `QImage` (`m_canvasBaseSurface`) with QPainter and then blitting it.
Every change of the cache key - which includes the split position - rebuilt the
whole surface, so dragging the divider rescaled both panes every frame. The new
B20 benchmark measures that composite at **149 ms per frame**.

The review's P2-09 asked for the canvas to move to GPU texture composition.

## Decision

1. **`m_compareCanvas` stays a plain QWidget.** Only the compositing step moves.
   `CompareCompositeGL` runs its own offscreen `QOpenGLContext` and renders the
   two pane rasters into an FBO with a fragment shader; the result comes back as
   a QImage that the existing paint code draws, followed by the unchanged pixel
   grid / ROI / filename overlays.
2. **The panes are still rasterised by the same CPU calls.** Each pane is drawn
   with the very same `drawCellCompare()` into its own QImage using that pane's
   geometry. That is what makes parity provable rather than hoped for: only the
   decision "which pixels of A and B land where" moved.
3. **Split / swipe / overlay composite in the shader.** Each branch is a 1:1 texel
   read - no resampling - so those three reproduce the old pixels exactly.
   `uDivider` comes from the pane boundary for Split (the contract splits at the
   midpoint) and from `m_splitPos` for Swipe.
4. **Checkerboard stays on the CPU path.** QPainter anti-aliases the `QRegion`
   clip at every block edge: a boundary pixel is a ~50/50 blend of both panes,
   while a shader switch picks one. Reproducing that analytically would mean
   copying QPainter's scan converter, and a hard switch is measurably wrong.
   `compositeCanvasWithGl()` declines, and the existing paint runs instead.
5. **Opt-in: `MVIEWER_COMPARE_GL=1`.** Qt's blitter/shader tiers have always been
   opt-in here, and this keeps the verified rendering path as the default until
   the compositor has been compared against more drivers.

## Consequences

- **Measured bit-exact.** Against `tests/gl_baseline` (v1.0.212 CPU captures),
  captured with `MVIEWER_COMPARE_GL=1` on Ubuntu 22.04 / Qt 6.8 / Mesa llvmpipe:
  **zero pixels differ beyond 2 codes** for split (both divider positions), swipe
  (both positions) and overlay, at scale factors 1.0 and 2.0. Checkerboard and the
  diff view take the CPU path, so they match by construction.
- The composite image carries the canvas' devicePixelRatio; without that tag
  QPainter scales a physical raster up again on HiDPI.
- The mode's own divider and the swipe drag handle are painted after the GL image
  (they used to live inside the base surface) - same pixels, same order relative
  to the other overlays.
- `tests/gl_baseline/README.md` documents how to re-capture and compare; a change
  to the split geometry, overlay alpha, checker size or diff threshold changes
  the baseline and must be re-captured, not tuned out of the comparator.

## RGB888 row layout regression (review P0-1)

`uploadCompareRgb888` makes a full RGB888 image copy to normalize external row
strides to Qt-owned, four-byte aligned storage. Upload uses alignment 4 and row
length 0, restoring both previous unpack values exactly; a null conversion/copy
fails before changing GL state. This adds a correctness copy, not a performance
claim. The compositor propagates upload rejection as a null result.

The required `build-linux` job explicitly runs
`QT_QPA_PLATFORM=xcb LIBGL_ALWAYS_SOFTWARE=1 xvfb-run -a
./build_linux/bin/compare_gl_rgb_stride_tests` after unchanged offscreen CTest.
This dedicated real-GL executable is built with the UI tests, not globally
registered with CTest; unavailable GL is a nonzero failure, never a CPU fallback
or skip. It checks both distinct panes at widths 5/6/7/8 over multiple rows,
valid extra-padded external rows, opaque alpha, exact unpack-state restoration,
null rejection and repeated target sizes. CI prints renderer/version and actual
assertion totals. These are proposed checks, not measured results until CI runs.
P0-2 owner-context teardown/caller restoration and full context-loss acceptance
remain separate; `destroy()` is unchanged by this stride-only repair.
