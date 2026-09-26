# Compare load smoothness (2026-09-26)

## Inventory (load path)

1. `CompareWorkspace::setImages` → DecodePool probe → `ImageLoadingService::loadFrameAsync`
   (ImageRepository cache shared with Browse).
2. `finishLoad` → `rebuildCells` → async display materialization + diff overlays.
3. Pair nav (`nextPair`/`prevPair`) re-enters `setImages` for the pool window.
4. Temporary hold uses `setTransientDisplay` (no reload).
5. Zoom/pan LOD refresh is debounced (~70 ms).

## Bottlenecks addressed

| Issue | Change |
| --- | --- |
| Pair switch blanked to full-page loading | Soft keep-grid when `imageCount() > 0` |
| No neighbor warmup | `preloadAsync` for prev/next nav window after `finishLoad` |
| Swap destroyed every pane widget | In-place frame/view swap (blink still rebuilds) |
| Smooth pixmap filter while dragging | Disabled during `m_dragging` |

## Deferred

- Progressive mipmap / new display pyramid (larger architecture).
- Promoting in-flight preloads on nextPair (optional; cache hit usually enough).
- Avoiding `rebuildCells` on layout preset changes (higher risk).

## Expectation

Pair navigation should keep prior rasters visible until the new batch materializes;
after one visit, stepping to the adjacent pair should more often hit a warm
ImageRepository entry. Not a hard CI budget change.
