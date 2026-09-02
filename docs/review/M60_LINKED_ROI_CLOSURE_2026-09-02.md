# M60 Closure Review — Linked ROI Measurement & RGB Ratios

Date: 2026-09-02
Release line: `1.0.17` (tag and GitHub run are filled after final push)

## Final report

1. **Was linked ROI already partially implemented?** Yes. `Selection`, right
   drag, `applySelectionToAll`, engine selection, ROI histogram/difference,
   session persistence, and ROI-aware analyzer/stat helpers already existed.
2. **What was missing?** Live sibling mirroring, the dimension guard, a source
   RGB measurement table/ratios/deltas, one authoritative format-aware result,
   latest-generation async work, and a bounded source-region path.
3. **What coordinate space owns ROI?** One canonical half-open rectangle in
   source-image pixel coordinates: `[x,x+w) × [y,y+h)`.
4. **Are different-size images proportionally mapped?** Never. They show a
   nonblocking `Linked ROI unavailable — image dimensions differ` status; a
   local active-pane ROI may remain useful without linked statistics.
5. **How are R/G and B/G defined?** `ΣR/ΣG` and `ΣB/ΣG`, equivalently the
   ratio of channel means over the exact same pixels.
6. **What happens when G == 0?** Both ratios are explicitly invalid and render
   as `—`; no NaN, infinity, or artificial value is emitted.
7. **Source or presentation RGB?** Source/analysis RGB. Display LOD, widget
   pixels, framebuffer captures, and ICC-converted presentation rasters are
   excluded.
8. **Does monitor ICC affect results?** No. M59 target-profile generations
   invalidate presentation work only; source measurements remain unchanged.
9. **Are Compare adjustments included?** No. The required baseline is labeled
   `ROI Measurement — Source RGB`; exposure/gamma/contrast/saturation are not
   silently folded into the measurement.
10. **How is high-bit-depth handled?** The source metadata remains truthful, but
    the current authoritative `ImageData` analysis boundary is 8-bit for paths
    without a higher-bit buffer. The UI/docs state this limitation rather than
    claiming 16-bit precision.
11. **How does a 100 MP ROI avoid UI blocking?** Geometry updates are cheap;
    stats run on the Analysis scheduler. Source-backed panes request a bounded
    region decode when supported instead of synchronously scanning on mouse
    move or full-decoding the source.
12. **Full source or display LOD?** Full-fidelity source/analysis pixels when
    available; the display LOD is never treated as an equivalent source.
13. **How are stale async results rejected?** Every batch has a generation and
    pane/geometry/link-state checks at queued delivery; cancellation and newer
    ROI/pair/clear/destruction invalidate older results.
14. **Does ROI survive zoom/pan/fullscreen?** Yes. It is reapplied through each
    pane's current image-to-widget transform.
15. **Does ROI survive Compare mode switching?** Yes. Grid/canvas/blink mode
    rebuilds retain the canonical geometry and statistics scope.
16. **Does CompareSession restore ROI?** Geometry is restored, then statistics
    are recomputed from the restored sources; stale numeric results are not
    persisted.
17. **What does next/previous pair do?** Matching dimensions retain and clip
    the canonical ROI; a differing pair disables linked measurement and avoids
    stale linked overlays/statistics.
18. **Are N-image comparisons supported?** Yes. All equal-dimension panes share
    one ROI and receive one result row; B−A deltas are limited to exactly two.
19. **What proves numerical correctness?** `m60_linked_roi_tests` covers
    reverse/outside/1px/zero/full geometry, RGB/BGR/RGBA/BGRA/grayscale,
    known means/ratios, zero-green invalidation, analyzer parity, and a
    multi-million-pixel 64-bit-count region. Compare acceptance covers live
    workflow state through mode switches and ROI histogram consistency.
20. **Did the final full gate pass twice source-stable?** Yes. The unchanged
    source tree passed the complete Release gate 129/129 twice: 820.73 s and
    822.00 s.

## Qualification record

| Gate | Result |
| --- | --- |
| Release build via `build.ps1 Release` | PASS |
| Focused Compare + M60 tests | PASS (1/1 Compare acceptance; 1/1 M60 core) |
| Full `build.ps1 Test` run 1 | PASS — 129/129, 820.73 s |
| Full `build.ps1 Test` run 2 | PASS — 129/129, 822.00 s |
| Complexity/architecture gates | PASS — 0 hard failures / 0 architecture violations |
| GitHub PR Gate | PASS — [run 33602280952](https://github.com/lgxgizh/mviewer/actions/runs/33602280952) (Tier 1) |
| GitHub Release package workflow | PASS — [run 33605323674](https://github.com/lgxgizh/mviewer/actions/runs/33605323674) (Tier 3) |
| GitHub release | Published — [v1.0.17](https://github.com/lgxgizh/mviewer/releases/tag/v1.0.17) |

## Known qualification boundary

Physical monitor LUT/HDR/mixed-DPI behavior remains a manual Windows review,
consistent with M59. Source-backed formats that cannot provide a full-fidelity
region reader are reported honestly; no display-LOD approximation is labeled
as source measurement.
