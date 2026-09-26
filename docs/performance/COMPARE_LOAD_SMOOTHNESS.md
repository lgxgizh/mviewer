# Compare load smoothness (2026-09-26)

## Inventory (load path)

1. `CompareWorkspace::setImages` → DecodePool probe → `ImageLoadingService::loadFrameAsync`
   (ImageRepository cache shared with Browse). Neighbor `preloadAsync` handles may be
   **promoted** into the batch via `promotePreloadAsync` when the user steps next/prev.
2. Soft keep-grid + progressive placeholders (caption + optional PreviewPanel cache)
   while the batch is in flight; per-pane 「加载中」 badge — never whole-page white when
   content already exists.
3. `finishLoad` → **in-place** pane update when kept frame count matches existing panes;
   otherwise `rebuildCells` → async display materialization + diff overlays.
4. Pair nav (`nextPair`/`prevPair`) re-enters `setImages` for the pool window.
5. Column-only layout / custom grid uses `relayoutGridKeepingPanes` (no pane destroy).
6. Temporary hold uses `setTransientDisplay` (no reload).
7. Zoom/pan LOD refresh is debounced (~70 ms); hist/diff deferred while interacting or
   soft-loading, flushed on settle.

## Bottlenecks addressed

| Issue | Change |
| --- | --- |
| Pair switch blanked to full-page loading | Soft keep-grid when `imageCount() > 0` |
| No neighbor warmup | `preloadAsync` for prev/next nav window after `finishLoad` |
| Prefetch unused on next/prev | `promotePreloadAsync` into the load batch |
| Swap destroyed every pane widget | In-place frame/view swap (blink still rebuilds) |
| finishLoad always rebuilt panes | In-place apply when pane count unchanged |
| Layout preset / column change rebuilt | `relayoutGridKeepingPanes` |
| Smooth pixmap filter while dragging | Disabled during `m_dragging` |
| Hist/diff during zoom/pan | Deferred until interaction settle |

## 极致快速 (beyond #47)

| Issue | Change |
| --- | --- |
| Zoom/first paint waits on full-edge LOD | `planCompareDisplayCheap` → provisional materialize (~640) then upgrade |
| Cold path without PreviewPanel cache | Blank panes still get cheap SourceImage::decodeLod ASAP |
| Display vs hist/diff pool contention | Provisional cheap display at `Priority::Decode`; full/adjust stay Analysis; hist/diff deferred |
| Browse→Compare blank first frames | `seedWarmDisplay` + `m_pendingWarmSeeds` reuse Viewer displayRaster |
| Canvas filter during gesture | SmoothPixmapTransform off while dragging / interactionBusy |


## 极致快速 Tier-1+2 (beyond #49)

| Issue | Change |
| --- | --- |
| Single cheap→full only | Multi-level display pyramid (~1/4, 1/2, 1×) + per-pane ready-level paint-through |
| Display starved by hist/diff/preload | Visible materialize at `Priority::Decode`; cancel display/hist/diff/roi on `setImages` |
| Pair switch re-decodes warm frames | `CompareSessionFramePool` LRU (path+frameIndex) beside ImageRepository |
| Zoom settle waits on Analysis | Gesture sets `forceDecodePriority`; predictive planner remains available |
| Multi-pane first paint jitter | Focus/edit pane scheduled first; pyramid cache paints immediately when covering |
| Browse handoff re-decode | `seedWarmDisplay` + `takeWarmDisplayRaster` for neighbor warms; softLoading kept |

## Deferred

- True mipmap/tile pyramid inside CacheManager / DecoderRegistry.
- Viewport-tiled region decode beyond existing region LOD.
- Low-precision live diff raster (currently defer-only until settle).

## Expectation

Opening Compare or switching pairs should show a usable raster sooner (warm Viewer
bitmap, preview cache, or cheap LOD), then sharpen without blanking. Zoom/pan
stays nearest-neighbor while the gesture is active.

## Expectation (#46/#47)

Pair navigation should keep prior rasters (or preview placeholders) visible until the
new batch materializes; after one visit, stepping to the adjacent pair should more often
hit a promoted/warm ImageRepository entry. Column changes should not flash blank panes.
Not a hard CI budget change.
