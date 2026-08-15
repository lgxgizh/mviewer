# M42 — Real-world Resource Boundedness & Large-image Compare Closure

Date: 2026-08-15
Status: implemented and locally verified

## 1. Baseline and reproducible evidence

The pre-M42 canonical gate was green at 88/88, but the new regression tests exposed three
resource-lifecycle defects:

- Raw16 was protected by `kRaw16MaxEntries = 2000`, not by bytes. `clearMemory()` only
  cleared the image cache; `erase()` and `invalidate()` did not remove the matching Raw16
  value; and cache accounting excluded Raw16.
- The first Compare acceptance fixture retained 92,160,000 display bytes for eight
  2,400 × 1,600 RGB24 sources: display memory equaled the full source payload. The
  tests-first LOD assertions failed before the implementation.
- RAW preview extraction used `QFile::readAll()` and then copied the complete file into a
  `std::vector<uint8_t>` for JPEG scanning. The tests-first allocation seam reported a
  second full-file allocation.

The baseline still passed the existing 88 tests; the failures above were newly added
regressions run against the baseline implementation before each fix.

## 2. Raw16 cache: before and after

| Concern | Before M42 | After M42 |
|---|---|---|
| Primary limit | 2,000 entries | 256 MiB byte budget |
| Accounting | ImageCache only | ImageCache + Raw16 bytes |
| `clearMemory()` | Left Raw16 resident | Clears Raw16 and releases values |
| `erase()` / `invalidate()` | Could leave Raw16 hit | Remove the corresponding Raw16 key |
| Oversized entry | Could be retained | Rejected without disabling `ImageFrame::raw16At()` |
| Concurrency | Not independently byte-accounted | Mutex-protected get/put/eviction/accounting |
| Eviction | Entry-count based | LRU byte eviction |

Raw16 values are now charged by their actual allocated vector capacity in bytes, evicted until the configured
budget is satisfied, and rejected when one value cannot fit. The owning `ImageFrame`
still keeps its own valid pixel storage, so Pixel Inspector remains correct after a
cache rejection or eviction.

Evidence:

- Cache regression: exact put/accounting, clear, erase, invalidate, byte eviction,
  oversized-entry behavior (including oversized reserved capacity), weak-owner
  release, and concurrent put/get.
- Repository soak: six deterministic RGBX64 PNG/TIFF loads, 41,472 bytes per image,
  82,944-byte cache budget; observed usage never exceeded the budget.
- `MemoryTracker` now reports Raw16 cache bytes, so a cache clear cannot report zero
  while Raw16 values remain accounted in the cache.

## 3. Compare display memory: before and after

The display path now materializes a viewport/DPR-sized LOD from the full-resolution
`ImageData` in the worker, applies display adjustments and ICC materialization there,
and atomically adopts only the latest valid result. The previous LOD remains visible
until replacement. LOD requests use a stable 1/16 density bucket, 1.25× overscan,
viewport/DPR sizing, 70 ms debounce, cancellation, and latest-wins delivery.

The full-resolution `ImageFrame` remains the source for analysis, PSNR, SSIM, ROI,
Pixel Inspector coordinates, and diff metrics. Only the visual diff overlay is reduced
to the display target; its geometry remains aligned with the display image.

For the deterministic eight-pane fixture:

| Metric | Before M42 | After M42 |
|---|---:|---:|
| Source bytes | 92,160,000 | 92,160,000 |
| Display bytes | 92,160,000 | 9,008,000 |
| Source/display ratio | 1.0× | 10.23× |

The hard assertion is that display bytes are less than half the source bytes; the final
run is below that threshold while preserving full-resolution analysis. The acceptance
flow also covers eight-image entry, zoom/pan, Diff, exit, and cleanup.

## 4. RAW preview allocation

`RawDecoder` now passes an immutable `std::span<const uint8_t>` over the `QByteArray`
returned by `QFile::readAll()` to the JPEG scanner. The scanner returns a non-owning
`QByteArray` view for the selected embedded preview, so the old complete-file vector
copy is gone. Existing behavior is covered for one preview, multiple previews (largest
selected), malformed/truncated input, no embedded preview, and ordinary JPEG input.

The allocation diagnostic reports zero bytes copied into a second full-file buffer for
the large-file path. A bounded streaming parser was intentionally not introduced: it
would expand parser risk without being required to close the observed duplicate-copy
bug.

## 5. Regression and product-path coverage

New or extended coverage includes:

- `test_cache`: Raw16 byte accounting, lifecycle, budget, ownership, and concurrency.
- `test_repository`: repeated 16-bit PNG/TIFF loading under a deterministic budget.
- `test_rawdecode`: embedded JPEG selection and malformed/truncated/no-preview cases.
- `compare_acceptance_tests`: real `CompareWorkspace` display materialization for eight
  synthetic large images, bounded display bytes, zoom/pan, Diff, exit, and analysis
  drain, plus zero pending/active/queued Analysis counters and zero scheduler graph
  handles after destruction.
- Existing workflow UX, lifecycle, render/display, cache, repository, Pixel Inspector,
  golden-image, benchmark smoke, and benchmark enforcement suites.

The deterministic materialization and byte-accounting invariants are hard-gate tests.
RSS and wall-clock observations remain report evidence because Windows allocator state,
Qt image backing storage, process startup, and GPU/driver behavior make RSS attribution
too noisy for a reliable cross-machine hard threshold.

## 6. Soak and performance evidence

The final eight-pane acceptance run reported:

```text
source bytes=92160000 display bytes=9008000
working-set KB before=195960 after=319564
```

The working-set increase includes the full-resolution source frames and process/runtime
effects; it is not used as the correctness assertion. The deterministic display-byte
invariant is the gate, and the 16-bit soak asserts the configured Raw16 budget after
each load.

## 7. Full gate results

The final code passed the canonical gate twice:

| Run | Result | Time |
|---|---:|---:|
| `powershell -ExecutionPolicy Bypass -File D:/mviewer/build.ps1 Test` | 88/88 | 417.17 s |
| `powershell -ExecutionPolicy Bypass -File D:/mviewer/build.ps1 Test` | 88/88 | 417.53 s |

Both runs included green `golden_image`, `bench_smoke`, `bench_enforce`,
`workflow_ux_tests`, `compare_acceptance_tests`, and lifecycle coverage. The focused
Compare acceptance suite also passed three consecutive runs (about 10 seconds each).

## 8. Risks not closed in this task

- RSS remains a useful soak signal, not a portable byte-level attribution mechanism.
- Unusual vendor RAW containers and ICC profiles still need broader real-world corpus
  coverage.
- Very high zoom can request larger LODs by design; the source frame remains the
  analysis authority and display LOD remains cancellable/latest-wins.
- The known TaskScheduler multi-pool oversubscription question was recorded but not
  changed because it is a frozen component and no M42 benchmark evidence justified a
  scheduler redesign.

## 9. Windows, GPU, and human UX review still required

The remaining sign-off should run on representative Windows machines with real GPU
drivers, multiple DPR settings, ICC display profiles, 2–8 large images, long zoom/pan
sessions, and representative RAW files. Human review must confirm no visible flicker,
white-frame exposure, stale pane delivery, zoom feel regression, or long-session
resource drift, and should run `docs/beta_checklist.md` end to end.

## 10. Self-review checklist

- Full-image resize, adjustment, and ICC work remains off the UI thread.
- Generation, cancellation, and latest-wins guards prevent stale adoption.
- No new unbounded cache or cache framework was introduced.
- Shared Raw16 values are removed from all requested lifecycle paths and byte-accounted.
- Analysis and Pixel Inspector use full-resolution source coordinates, not display pixels.
- ICC display materialization and existing Compare modes remain on the existing path.
- `compareworkspace.cpp` is 791 lines, below the ADR-014 800-line limit; the shortcut
  responsibility moved to `compareworkspace_interact.cpp`.
- Build scripts, CMake presets, CI, Scheduler, and other frozen architecture were not
  modified.
