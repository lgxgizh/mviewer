# M57 — Multi-frame / Multi-page closure report

Date: 2026-08-29
Scope: animated GIF, animated WebP, multi-page TIFF
Status: automated closure complete; native qualification boundaries are recorded below.

## Result summary

MViewer now models a source as a sequence capability rather than teaching the
Viewer special cases for individual formats. GIF/WebP are time sequences;
multi-page TIFF is a page sequence. The gallery remains one item per file,
thumbnails remain static, and the current frame/page is explicit from decode
through Compare, metadata, analysis-facing ImageFrame state, persistence, and
export.

## Architecture and identity

`mviewer::core::FrameSequenceReader` owns `probeSequence`, `decodeFrame`, and
`decodeFrameScaled` through the Qt-free `FrameSequence.h` contract. The Qt
`QImageReader` implementation is isolated in `FrameSequence.cpp`. Ordinary
JPEG/PNG/BMP sources report a valid one-frame static sequence, so existing
static callers keep their behavior.

`FrameIdentity` is:

```text
existing file revision key + frame/page index + decode variant
```

The existing M56 revision identity remains in the key. Repository memory and
disk-facing frame loads therefore cannot satisfy frame 1 from frame 0, and
bounded prefetch variants cannot collide with native/full frames.

## Playback, boundedness, and failure behavior

`FramePlaybackController` uses `std::chrono::steady_clock` presentation time,
per-frame durations, and catch-up/skip decisions. Decode and prefetch remain
worker operations. The Viewer keeps at most two bounded 512-edge neighbor
prefetch requests, cancels obsolete requests on navigation/close, and ignores
late completions by path plus generation. It does not materialize an entire
animation or TIFF page set.

The Viewer preserves the active zoom/pan transform while adopting a frame.
`,` and `.` step frames/pages; Space toggles animated playback; the existing
left/right file navigation remains unchanged. Context actions expose Play,
Pause, Restart, Previous/Next Frame or Page. A corrupt requested frame pauses
playback and reports the requested frame/page failure without crossing the
Viewer boundary or retrying in a busy loop.

Preview and thumbnails use only frame/page zero. Thumbnails remain singular
and static, with `Nf` / `Np` badges. Compare is non-animated and stores one
frame/page index per pane; its compact frame control reloads only the selected
pane while retaining the existing Compare session transforms.

## Answers to the M57 qualification questions

1. **Before M57:** the path-only pipeline decoded one image. Qt advertised
   multi-frame GIF/WebP and three-page TIFF, but the product did not expose
   that capability. See the Phase 0 baseline.
2. **Previously first-only:** GIF, WebP, and multi-page TIFF were effectively
   first-frame/first-page in Viewer, Preview, Compare, Analyze, and export.
3. **Frame identity:** M56 file revision + frame/page index + decode variant.
4. **Decoder capability:** `FrameSequenceInfo`, `FrameInfo`, and
   `FrameDecodeResult` behind `FrameSequenceReader`; no format branch in the
   Viewer and no parallel static/animated pipeline.
5. **Timing:** a monotonic steady-clock timeline anchored to presentation
   timestamps; a slow decode is caught up/skipped instead of added to every
   frame interval.
6. **Prefetch memory:** worker decode, latest-generation cancellation, two
   bounded 512-edge neighbor requests, and repository cache identity including
   variant and frame.
7. **1000/10000-frame RSS:** the implementation probes in O(1) memory and
   decodes only requested/sequential-to-requested frames. A dedicated real
   10,000-frame 8K RSS qualification was not run in this environment; the
   bounded fake timeline/catch-up contract is covered by the M57 test.
8. **GIF disposal/transparency:** qgif's composed `QImage` result is copied
   before conversion, preserving the plugin's composition/alpha result. The
   deterministic real-plugin fixture verifies distinct composed colors; a
   hostile disposal/dirty-rectangle corpus remains manual follow-up.
9. **100MP multi-page TIFF:** page selection is on-demand and does not
   enumerate/decode all pages. The existing M53 source-backed large-image
   contract remains the governing path; physical 100MP multi-page RSS was not
   available for this run.
10. **Compare:** each pane carries a frame/page index; load, session JSON, and
    the Compare control preserve and reload that explicit value.
11. **Analyze:** `ImageFrame` metadata and pixels identify the selected
    frame/page, and the metadata panel refreshes from the current frame. The
    existing analysis consumers receive the current `ImageFrame`, not a new
    path-only frame.
12. **Workspace:** current path, frame index, and playing state are persisted;
    Compare pane indices are persisted. Legacy schemas default to frame zero
    and paused.
13. **Package plugins:** the Release build deploys `qgif.dll`, `qwebp.dll`, and
    `qtiff.dll` beside the executable; the real-plugin M57 test loads all three
    through Qt's runtime imageformats directory.
14. **Before/after:** before: no frame/page identity, no playback/page
    navigation, and path-only cache. After: explicit sequence/identity,
    real GIF/WebP frame decode, TIFF page navigation, bounded prefetch,
    persistence, and Compare frame selection. Exact test timings and counts
    are recorded in `STATUS.md` and the changelog: two source-stable Release
    gates passed 124/124 in 755.11 s and 753.71 s.
15. **Automated gates:** `m57_multiframe_decoder_tests` covers real qgif/qwebp/
    qtiff fixtures, repository cache identity, Compare/session round-trip,
    timing catch-up, workspace round-trip, and legacy defaults. The two full
    canonical Test passed 124/124 twice consecutively; architecture violations
    and complexity hard failures were both zero.
16. **MANUAL/BLOCKED:** physical native GUI UX, mixed-DPI/ICC/UNC behavior,
    packaged shipping launch on a clean target machine, long-session playback
    feel, hostile disposal corpus, and 100MP multi-page TIFF RSS/soak remain
    manual qualification items. They are not presented as automated passes.

## Automated and packaging evidence

The focused test uses deterministic fixtures generated at runtime and fails if
the deployed Qt handlers do not report/decode three GIF frames, three WebP
frames, and three TIFF pages with red/green/blue pixel identity. The same test
also verifies frame-specific repository cache hits, Compare frame identity,
monotonic timing, bounded-stall catch-up, workspace persistence, and legacy
schema defaults.

The canonical gate is the repository entry point:

```powershell
.\build.ps1 Test
```

It is required to pass twice consecutively after the final source revision,
including the existing workflow, golden-image, benchmark, large-source,
M54/M55 performance, and M56 live-folder gates. Architecture violations and
complexity hard failures must both remain zero.
