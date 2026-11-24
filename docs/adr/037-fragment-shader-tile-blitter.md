# ADR 037 — Fragment-shader tile blitter, opt-in behind an A/B switch

Date: 2026-11-24
Status: Accepted

## Context

The GPU tile path composites with `QOpenGLTextureBlitter`, which can only copy a
texture. Every per-pixel effect therefore has to happen on the CPU before the
upload: exposure / gamma / AutoStretch are baked into the `ImageData`
(`applyDisplayAdjust`), the analysis overlays are derived per tile by a worker
(`scheduleOverlayTiles`). That is why v1.0.211 still falls back to `QPainter`
for the whole tile layer whenever an overlay mode is active - there was no way
to draw the derived tiles through GL in that state.

The review's P2-08 asks for a single fragment shader carrying exposure, gamma,
the 3D LUT and the overlay, so no frame ever needs the CPU compositor.

## Decision

1. **A small shader class, not a rewrite of the paint path.**
   `gpu/ImageBlitterShader` owns one `QOpenGLShaderProgram` plus a dynamic vertex
   buffer and draws a triangle strip. The vertex shader takes clip-space
   positions and UVs; the fragment shader samples the tile, applies
   `uExposure` / `uInvGamma`, optionally alpha-blends a second (`uOverlay`)
   texture, and writes opaque output. `blitPlacedTile` keeps its existing
   geometry maths and simply chooses between the two backends.
2. **Opt-in: `MVIEWER_BLITTER=shader`.** Qt's blitter remains the default, so
   the verified rendering path does not change for anyone who does not ask for
   this. `ImageBlitterShader::requested()` caches the environment read;
   `resetRequestedCache()` lets tests flip it.
3. **A failure to build or link falls back silently.** `create()` returns false
   and the caller keeps using `QOpenGLTextureBlitter`.
4. **No adjust wiring yet.** Exposure and gamma are already baked into the
   uploaded pixels by `imageviewer_adjust.cpp`; feeding them to the shader as
   well would double-apply. The uniforms are in place for the day the pipeline
   skips the CPU bake (which is the rest of P2-08).

## Consequences

- The shader path was compared against `QOpenGLTextureBlitter` on real OpenGL
  (Ubuntu 22.04 / Qt 6.8 / Mesa llvmpipe, core profile 4.5) with a framebuffer
  round trip: same texture, same destination rect, same origin - **maximum
  per-channel difference 0**. That check lives outside the repo on purpose (it
  needs a window system); it is described in
  `tests/gl_baseline/README.md` alongside the compare-canvas baselines.
  Note that `QOpenGLTextureBlitter::blit()` reports "Unsupported texture target"
  unless `bind()` has been called first; MViewer already binds before blitting.
- Pixel parity has been established for the **copy-only** case. Exposure /
  gamma are still baked CPU-side (decision 4), so there is nothing further to
  compare until the pipeline stops baking them.
- Turning it on remains a manual action; a defect cannot reach a user who has
  not asked for it, and `MVIEWER_GPU=0` still bypasses the whole tier.
