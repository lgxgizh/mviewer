# ADR-M22.4: Analysis Overlays (zebra / false-color / scope)

## Status

Accepted — shipped in M22. Live zebra / false-color overlay is embedded in
`ImageViewer` (right-click menu, deep-copied tiles so the TileCache buffer is
never mutated); waveform / vectorscope remain in the standalone
`AnalysisOverlayDialog`.

## Context

The analysis suite is metric-rich (histogram, PSNR, SSIM, MTF, dead-pixel,
ColorChecker ΔE, …) but offers no at-a-glance *visual* judgement aid that ISP /
color engineers use while browsing (clipping zebra, false-color, waveform,
vectorscope).

## Decision

Add three overlay capabilities, all off by default:
- **Zebra**: diagonal hatch on pixels ≥ high threshold (default 98%) or ≤ low
  threshold (default 2%); threshold configurable in Preferences (analysis tab).
- **False-color**: map luminance (or chosen channel) through a perceptual
  colormap (inferno/jet/turbo) as a viewer overlay mode.
- **Waveform / Vectorscope**: scope widgets fed from the current frame / ROI,
  plotted in `AnalysisPanel`.

Overlay rendering reuses the existing `RenderEngine` `DrawOverlay` path
(`RenderCommandType::DrawOverlay` already exists); scope widgets are plain
`QWidget`s in the UI layer.

## Rationale

- Completes the visual-analysis toolkit for the professional positioning.
- Pure UI + RenderEngine overlay; no frozen module touched.
- Defaults-off ⇒ no change to current look/behavior.

## Consequences

- ✅ At-a-glance clipping / gradient / chroma judgement.
- ✅ Reuses existing overlay command path (no new render abstraction).
- ❌ Scopes are display-only (no export of scope image in v1).

## Related

- RFC M22 §F4
- `src/imageviewer.cpp` (overlay paint), `src/core/render/RenderEngine.h` (DrawOverlay)
