# RFC-006: Compare Engine Controllers

## Status

Implemented

## Priority

P0

## Goal

Split CompareEngine into dedicated controller subsystems.

## Current State

CompareEngine is a monolith handling sync, blink, diff, selection, viewport, and layout.

## Target Architecture

```
CompareEngine (facade)
    ├── SyncController — shared zoom/pan/scroll
    ├── BlinkController — alternating highlight
    ├── DifferenceEngine — pixel diff + heatmap
    ├── SelectionController — ROI box sync
    └── ViewportController — per-cell transform state

CompareSession owns all comparison state.
Workspace only renders state.
UI never owns comparison logic.
```

## Controllers

### SyncController

- Manages shared scale/offset across cells
- Handles zoom-at-point, pan, fit
- Emits transformChanged signal

### BlinkController

- Timer-based alternating highlight
- Configurable interval
- Emits blinkChanged signal

### DifferenceEngine

- Computes pixel diff between two images
- Generates grayscale diff map
- Generates heatmap (pseudo-color)
- PSNR/SSIM calculation

### SelectionController

- Manages ROI rectangle per cell
- Syncs selection across cells (optional)
- Emits selectionChanged signal

### ViewportController

- Per-cell transform (scale, offset)
- Cell layout (cols, rows)
- Cell size calculation

## Consequences

- Clean separation of comparison concerns
- Each controller tests independently
- More files (~6 vs 1)

## Related

- ADR-009 (Why split compare engine)
- ADR-003 (CompareSession)
