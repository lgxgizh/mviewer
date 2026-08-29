# M55 Phase 0 — Interactive Latency, Memory and Navigation Baseline

Date: 2026-08-29 · Milestone: M55 · Baseline tree: `master` at `e07f1f0`

This is the control record captured before the M55 implementation changes.
The M54 executable drove real `ThumbnailPanel`/`PreviewPanel` instances over
temporary image directories. Times are milliseconds on this Windows host; they
are comparative evidence, not cross-machine acceptance thresholds.

## Existing Browse/Preview measurements

| Corpus | State | first row | first thumb | screen 50% | screen 90% | preview p50/p95 | scan/stable |
|---|---:|---:|---:|---:|---:|---:|---:|
| 100 | cold | 80 | 3833 | 3969 | 4110 | 13 / 48 | 68 / 68 |
| 100 | warm | 32 | 66 | 128 | 193 | 3 / 40 | 15 / 15 |
| 1,000 | cold | 38 | 117 | 309 | 436 | 4 / 29 | 202 / 202 |
| 1,000 | warm | 42 | 70 | 174 | 254 | 11 / 44 | 304 / 304 |
| 10,000 | cold | 59 | 125 | 274 | 460 | 26 / 48 | 2476 / 2584 |
| 10,000 | warm | 30 | 81 | 160 | 227 | 23 / 44 | 2479 / 2479 |
| 50,000 | cold | 41 | 148 | 298 | 492 | 28 / 53 | 11820 / 12158 |
| 50,000 | warm | 67 | 112 | 201 | 263 | 23 / 34 | 11492 / 11810 |

The same run reported `scroll_jump_p95=50`, `peak_thumbnail_queue=112`, and
`wasted_decode_work=44`. PNG encoding p50 was 16 ms at default compression and
7 ms at fast compression; the fast payload was materially larger, so this was
not changed blindly.

## Characterized gaps

- The UI held `QPixmap` values by raw path with no count/byte budget and emitted
  one decoration update for the whole model after each result.
- Same-generation viewport changes cancelled the complete pipeline working set;
  generation-only bookkeeping left an ABA window when a running decode was
  cancelled and the same key was immediately re-submitted.
- The progressive scanner locked the probe hook on every iteration and its
  final comparator converted every `QString` to `std::string` repeatedly.
- The first exact thumbnail-cache access indexed the entire on-disk history
  while holding the cache mutex.
- Viewer neighbor warmth was full-frame-only and gated on `m_frame`, so the
  100MP LOD-first viewer had no bounded display-raster neighbor path.
- `TagStore` synchronously rewrote the complete tags file after every edit;
  this was a clear UI write hotspot by inspection against the already-proven
  `RatingStore` coalescing contract.

## Measurement boundary

The new repeatable recorder is `m55_interactive_baseline`; it records 10k/50k
working-set/private-byte peaks, pipeline pending/handle peaks, Browse
milestones, and scroll-jump p50/p95. It is a standalone recorder rather than a
pass/fail CTest because the values are machine-dependent. Native fullscreen
sequential-next feel and extended physical monitor smoothness remain
`MANUAL/BLOCKED` until run in a real desktop session.
