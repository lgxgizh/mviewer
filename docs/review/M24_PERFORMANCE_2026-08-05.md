# M24 Phase 7 — Performance & Resource Stability (2026-08-05)

Real-scale measurements on the M24 machine (2-core Xeon Platinum 2.5 GHz VM,
Release build). Tool: `benchmarks/m24_soak_main.cpp` →
`build_msvc\bin\mviewer_m24_soak.exe` (NOT a CTest gate; run manually/nightly,
`--out results.json`).

## Root cause found and fixed: multi-second UI stall on large directories (P1)

**Repro:** opening a 10000-image folder stalled the UI thread ~2.7 s after the
entries arrived (measured via an 8 ms timer gap monitor).

**Bisect evidence:** the stall required a visible viewport (0×0 → 41 ms;
1200×800 → 2.7 s); persisted with single-threaded scheduler (not CPU
starvation), with List view (not the dimension pass), and after Batched layout
(not the model insert — `setStringList(10000)` is 24 ms standalone).

**Root cause:** `ThumbnailPanel::updateVisibleRange()` used
`indexAt(QPoint(2,2))` / `indexAt(viewport-2)` to find the visible rows. Right
after `buildModel`, the view's geometry is stale (Batched layout defers it), so
`indexAt()` forced a **full layout pass over all 10000 rows** — ~2.5 s on this
VM, per directory change.

**Fix:** compute the visible range arithmetically from grid size + viewport +
scroll offset (exact for the uniform grid; `setUniformItemSizes(true)` already
guarantees uniform geometry). Bonus hygiene: `dataChanged` per decoded
thumbnail is now coalesced into one flush per event-loop turn.

**Measured after the fix (default config):**

| Scenario | Before | After |
|---|---|---|
| 10000-image folder full load | 3626 ms | **1145 ms** |
| UI-thread max stall during load | 2753 ms | **184 ms** |
| First entries visible | ~1075 ms | ~1075 ms (unchanged — QDir sort) |
| Worst folder switch (5000-image) | ~590 ms | ~583 ms (unchanged — scan bound) |

No product regression: `thumbnailpipeline_tests`, `async_lifetime_tests`,
`browse_acceptance_tests`, `workflow_ux_tests`, `bench_smoke` (incl. the
pipeline-priority trace) all pass.

## Measured soak results (final, default config)

| Metric | Value | Notes |
|---|---|---|
| S1 10000-image scan to full gallery | 1145 ms | QDir sort dominates (~1.1 s on 2-core VM) |
| S1 first entries | 1129 ms | shell paints instantly (M23 P2) |
| S2 worst of 20 rapid 5000-image switches | 583 ms | scan-bound |
| S3 UI-thread max stall during load | 184 ms | fixed (was 2753 ms) |
| S4 24 MP JPEG cold decode | 3525 ms | single-threaded libjpeg, 2-core VM |
| S5 4000×4000 TIFF cold decode | 2126 ms | qtiff |
| S6 corrupt-mixed 1000-image browse | 3.1 s (incl. 3 s settle) | no crash, failures placeholdered |
| S7 steady RSS / handles | 35 MB / 223 | after galleries released |
| S7 exit | ~0 ms | clean |

## Findings recorded (no gate changes made)

1. **B10 (100 MP viewport)** remains report-only by design; measured 1480 ms on
   this VM vs 392.8 ms on the baseline box (hardware capability; Phase 1 doc).
2. **24 MP decode (3.5 s) and 4K TIFF (2.1 s) cold** on a 2-core VM: decode is
   single-threaded. This is the largest single latency in the main flow; the
   product already keeps it off the UI thread (ImageRepository + scheduler).
   Proposal (not executed): thread the JPEG/TIFF decode path or document the
   per-core scaling for the target hardware.
3. Thread oversubscription: default scheduler pools size to `idealThreadCount`
   (4 on 2 vCPUs) — the S3 bisect showed this adds latency but does not stall
   the UI; a `max(1, cores-1)` cap would help low-core machines (scheduler is
   frozen — recorded for the commander).

## Tooling

`mviewer_m24_soak.exe` is a permanent, non-gating soak tool with an optional
`MVIEWER_SOAK_1THREAD=1` diagnostic mode (isolates CPU starvation) and
`--out results.json`.
