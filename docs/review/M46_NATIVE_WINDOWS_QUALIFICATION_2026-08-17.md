# M46 — Native Windows Release Qualification Checklist

Date: 2026-08-17 · Milestone: M46 (Real-World Workflow Reliability & Long-Session
Release Qualification)

## How to read this report

Every row is one release-qualification item. Status values:

- **PASS (automated)** — proven by an automated regression in `.\build.ps1 Test`
  on this machine; the evidence column names the test.
- **MANUAL — hardware/environment unavailable** — the item needs an interactive
  desktop, physical display hardware, or a target-machine condition this
  offscreen VM cannot provide. It is NOT converted to PASS by green CTest runs.
- **BLOCKED — requires <condition>** — same principle, explicitly named.

No item is marked PASS solely because the automated suite is green.

## Workflow chain (Browse → Viewer → Compare → Analyze → Export → FileOps)

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 1 | Browse a large folder (10k+ entries) without UI stall; first thumbnails fast | PASS (automated) | `m46_browse_tests` (scan supersession bounds, paint-path freedom), `bench_enforce` B1/B2/B7, M24 soak S1/S2/S3 |
| 2 | Rapid thumbnail scrolling keeps latest visible range prioritized; idle returns scheduler/pipeline to 0 | PASS (automated) | `m46_thumbnail_tests` T1–T4 (mid-flight resize supersession, idle convergence), `m27_thumbnail_tests` |
| 3 | Thumbnail-size drag supersedes old-size decodes (no unbounded stale workload) | PASS (automated) | `m46_thumbnail_tests` T1/T2 (stale-size deliveries = 0, cache holds only final size) |
| 4 | Directory switch A→B→C stops superseded walking/sorting/dimension probing cooperatively | PASS (automated) | `m46_browse_tests` B3/B4 (bounded iteration probes after switch) |
| 5 | Viewer open → next/previous → zoom/pan; destroy mid-decode and A→B→A are race-safe | PASS (automated) | `m46_browse_tests` B6/B7, `m46_repository_tests` (delivery gate), `m27_lifetime_tests` |
| 6 | Compare pair change + workspace destroy mid-load is race-safe | PASS (automated) | `m46_browse_tests` B8, `m46_repository_tests` C3/C4 |
| 7 | Analyze (stats/PSNR/SSIM) on decoded frames in the loop | PASS (automated) | `m46_workflow_soak` analyze step, `analyze_acceptance_tests` |
| 8 | Export completes; export cancel mid-flight leaves no partial/temp residue | PASS (automated) | `m46_workflow_soak` export step (pre-cancelled + mid-flight cancel), `export_job_tests` |
| 9 | Long-session resource convergence (RSS/handles/pools/cache) | PASS (automated, short form) | `m46_workflow_soak --iterations 8` (CTest); extended form manual: `--extended` |
| 10 | Interactive full workflow on a real desktop with a real mouse/keyboard | **MANUAL — hardware/environment unavailable** | requires interactive target desktop |
| 11 | Perceived smoothness / flicker / zoom feel during the loop | **MANUAL — hardware/environment unavailable** | UX Review Agent on target desktop |

## Display & color

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 12 | 100% DPI run | **MANUAL — hardware/environment unavailable** | requires interactive display session |
| 13 | 125% DPI run | **MANUAL — hardware/environment unavailable** | requires interactive display session |
| 14 | 150% DPI run | **MANUAL — hardware/environment unavailable** | requires interactive display session |
| 15 | 200% DPI run | **MANUAL — hardware/environment unavailable** | requires interactive display session |
| 16 | Mixed-DPI dual monitor (per-monitor DPI) | **BLOCKED — requires two physical displays with different DPI** | offscreen platform cannot create mixed-DPI monitors |
| 17 | Physical ICC profile changes while running (profile switch / unplug) | **MANUAL — hardware/environment unavailable** | requires a real monitor + installed physical profiles; ICC parsing itself is covered by `iccprofile_tests` / `m36_display_tests` (display materialization of embedded profiles) |
| 18 | Display pipeline honors embedded ICC on display copies only | PASS (automated) | `m36_display_tests` (sRGB/AdobeRGB/Display-P3/no-profile) |

## File operations (native Windows)

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 19 | FileOps across two real physical volumes (copy/move with cross-volume fallback) | **BLOCKED — requires two writable physical volumes** | this environment has a single writable volume; the cross-volume code path itself is covered by `test_commandstack` "cross-volume rename fallback" (fault-injected adapter) |
| 20 | Native Windows open/save dialogs (file dialogs with shell UI) | **MANUAL — hardware/environment unavailable** | interactive native dialogs cannot be driven offscreen; non-interactive dialog-less paths are covered by `mainwindow_commandstack_acceptance` |
| 21 | Unicode / Chinese / emoji filenames round-trip through rename/undo | PASS (automated) | `test_commandstack` (Unicode dir + 文件-😀.txt round-trip) |
| 22 | Long paths (> 260 chars, extended-length) | **MANUAL — hardware/environment unavailable** | needs a real volume with long-path support enabled; `test_commandstack` covers cross-volume/Unicode but not the OS long-path policy |
| 23 | Special filenames (leading dots, trailing spaces, reserved names) | **MANUAL — hardware/environment unavailable** | reserved names (CON/NUL…) are volume-policy dependent |
| 24 | Cancel/close during active FileOps (transfer progress + cancel) | PASS (automated) | `test_commandstack` (cancellation safety), `mainwindow_commandstack_acceptance` |
| 25 | Rapid window open/close (viewer/compare churn) | PASS (automated) | `m46_browse_tests` B6/B8, `m27_lifecycle_torture` (100 rounds) |

## Long-session resources

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 26 | Long session RSS/handles on the target desktop | **MANUAL — hardware/environment unavailable** | the automated short soak (`m46_workflow_soak`) proves the convergence mechanics; the multi-hour interactive session must be observed on target hardware |
| 27 | Automated soak convergence: pools → 0, pipeline → 0, cache obeys caps, RSS ≤ max(128 MiB, 15%), handles ≤ +64 | PASS (automated) | `m46_workflow_soak` (CTest short form); extended manual: `--extended` |

## Release artifacts

| # | Item | Status | Evidence |
|---|------|--------|----------|
| 28 | Portable ZIP / installer launch on a clean Windows (PATH stripped) | PASS (historical) | M24 g1 clean-windows proof (2026-08-05); re-run on demand |
| 29 | Crash handler produces a minidump on demand | PASS (automated) | `crashhandler_tests` |

## Sign-off

The rows above are the M46 native qualification contract. Rows 10–17, 20, 22–23,
26 must be executed on the target Windows desktop and attached to the release
evidence (screenshots + logs) before a native hardware sign-off can be claimed.
They are intentionally NOT marked PASS by the automated gate.
