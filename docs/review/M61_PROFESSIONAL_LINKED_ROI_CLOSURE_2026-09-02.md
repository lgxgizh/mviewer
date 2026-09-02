# M61 Professional Linked ROI Workflow — Closure Review

Date: 2026-09-02

Patch line: `1.0.18`

Phase 0 baseline: `M61_PHASE0_ROI_WORKFLOW_BASELINE_2026-09-02.md`

## Verdict

**Automated closure: PASS.** M61 turns the M60 measurement primitive into one
editable, failure-honest workflow across all Compare presentation modes. No
second ROI model, scheduler redesign, decoder-registry change, or CI/build-system
change was introduced.

## Closure questions

1. **Visible/operable modes:** real right-button UI tests create and mirror the
   ROI in Grid, Split, Overlay, Swipe, and Checkerboard; move and all edge/corner
   semantics share one helper.
2. **Canonical coordinates:** one half-open `Selection` in displayed source
   pixels is stored and persisted. Canvas and Grid are projections only.
3. **Mapping drift:** pure mapping tests and Canvas→Grid→Canvas UI transitions
   preserve exact integer source geometry, including reverse/outside/1×1 cases.
4. **Cancellation:** scan work checks at row boundaries. The focused benchmark
   cancelled after nine checkpoints and exited in **193 µs** on this host.
5. **Scheduler rejection:** a paused/rejecting Analysis pool produces visible
   terminal `Backpressured`; retry after resume reaches `Ready`.
6. **Decoder bounded truth:** JPEG and Windows unrotated WIC TIFF explicitly
   declare bounded source pixels. PNG/BMP/other unproven Qt paths do not.
7. **Unsupported behavior:** unsupported paths keep geometry visible and show a
   per-pane reason; they never substitute display LOD pixels.
8. **ICC:** statistics consume immutable source/analysis bytes. M59 presentation
   conversion affects display only.
9. **Compare adjustments:** crop/rotation/display adjustments are presentation
   concerns and do not mutate source measurement values.
10. **16-bit truth:** current analysis is honestly labeled 8-bit. Native 16-bit
    statistics remain unsupported rather than silently claimed.
11. **Stale delivery:** generation, geometry, linked state, pane count, and
    `QPointer` lifetime guards reject A→B→A, clear, navigation, and destruction
    results that no longer match.
12. **Hidden side panel:** a compact non-modal HUD retains state/results, avoids
    the ROI center, and opens the complete table when clicked.
13. **100 MP source:** proven JPEG/WIC-TIFF region paths do not require a client
    full decode for a small ROI. Unproven formats are rejected by preflight
    before pixel decoding. The focused benchmark also records worst-case
    in-memory full scans.
14. **Real input:** the focused and workflow gates send real right-button
    press/move/release events to production `RawImageView` and Canvas widgets;
    programmatic `applyROI()` is not the sole evidence.
15. **Manual boundary:** pointer feel, contrast on a physical calibrated/HDR
    monitor, long-session native GUI smoothness, and target-machine packaging
    remain MANUAL/BLOCKED in this non-interactive environment.

## Focused evidence

The initial RED run failed eight production expectations: existing-ROI move,
four Canvas modes, hidden HUD, clipboard action, and backpressure state. After
implementation, `m61_roi_workflow_tests` passes without weakening prior tests.

`m61_roi_benchmark` on this host:

| Scan | Pixels | Time |
| --- | ---: | ---: |
| 24 MP grayscale | 24,000,000 | 81 ms |
| 60 MP grayscale | 60,000,000 | 210 ms |
| 100 MP grayscale | 100,000,000 | 319 ms |
| Cancellation | 9 row checkpoints | 193 µs to exit |

## Full local gates

Final source-stable Release verification is recorded before commit:

- Run 1: `powershell -ExecutionPolicy Bypass -File D:\mviewer\build.ps1 Test`
  — **131/131 passed**, 814.15 s.
- Run 2: identical command and unchanged source — **131/131 passed**, 795.36 s.

Both runs include version/release consistency, workflow UX, architecture and
complexity gates, golden image, `bench_smoke`, and `bench_enforce`.

## Self-review and limitations

- Build system, presets, CI, Scheduler architecture, DecoderRegistry, cache,
  plugin framework, and workspace architecture remain untouched.
- `compareworkspace.h` remains below the 800-line hard cap; ROI value/state
  types and pure interaction/mapping responsibilities live in separate headers.
- No native 16-bit ROI accumulator was added. No proportional linking for
  unequal dimensions was invented. Decoder cancellation cannot interrupt a
  blocking third-party decode already in progress; row scanning is promptly
  cancellable once pixels are available.
