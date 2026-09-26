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

## Deferred

- Progressive mipmap / new display pyramid (larger architecture).
- CacheManager / DecoderRegistry rewrites.

## Expectation

Pair navigation should keep prior rasters (or preview placeholders) visible until the
new batch materializes; after one visit, stepping to the adjacent pair should more often
hit a promoted/warm ImageRepository entry. Column changes should not flash blank panes.
Not a hard CI budget change.
