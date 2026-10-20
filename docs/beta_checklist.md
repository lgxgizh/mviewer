# MViewer v0.9 Beta Checklist

> **Purpose**: v0.9 Beta regression gate. After every PR and every feature landing, the **UX Review Agent** (see the role definition below) runs this checklist in full. Merge is allowed only when every item is checked. The goal is to move the product from a demo under development to a product that can be used long-term.
>
> **M53 release-quality review snapshot (2026-08-29)**
> - M53 focused qualification covers 100MP TIFF Viewer/Compare bounded display,
>   16-bit TIFF plus large PNG/BMP boundaries, Unicode paths, streaming RAW
>   preview scanning, and a five-round large-source lifecycle soak.
> - Final local automated gate: `build.ps1 Test` passed **132/132 twice
>   consecutively** (793.12 s and 792.20 s); `m51_rc_soak` passed in both runs.
> - M53 package contract: **PASS** for the 1.0.13 portable ZIP and installer;
>   physical target-machine UX remains MANUAL/BLOCKED.
> - Automated quality truthfulness: **PASS** means the measured hard-failure
>   count is zero. Advisory complexity warnings are recorded as **accepted
>   baseline debt**, not silently presented as a clean codebase.
> - Automated boundary coverage includes PowerShell 5.1 UTF-8/mojibake,
>   honest responsibility-TU caps, pure source-backed Compare planning, and
>   real MainWindow navigation/Compare adversarial cases.
> - Source-backed large-image truth: **100 MP JPEG and 100MP-class TIFF are
>   implemented** through bounded source-backed display; TIFF uses the measured
>   Windows WIC path, while PNG/BMP retain honest scaled-fallback classification.
> - Native UX review: **MANUAL PENDING / BLOCKED** for physical DPI/ICC/UNC,
>   installer behavior, long-session GUI feel, and extended soak on Windows.
>
> **M57 multi-frame review snapshot (2026-08-29)**
> - Deterministic real-plugin coverage is in place for three-frame GIF, three-frame
>   WebP, and three-page TIFF. The baseline records qgif/qwebp sequential-read
>   behavior and qtiff page random access in
>   `docs/review/M57_PHASE0_MULTIFRAME_BASELINE_2026-08-29.md`.
> - Viewer playback/page navigation is bounded and cancellable; frame/page status,
>   Compare pane identity, workspace restore, metadata, and static export use the
>   selected frame. Preview and thumbnails remain static frame/page zero.
> - Automated closure: two consecutive source-stable Release `build.ps1 Test`
>   gates passed **124/124** in 755.11 s and 753.71 s; architecture violations
>   and complexity hard failures were both zero.
> - UX review is **MANUAL PENDING / BLOCKED** for native playback feel, mixed-DPI
>   zoom continuity, target-machine packaging, hostile disposal fixtures, and
>   long-session/100MP multi-page RSS qualification.
>
> **Pass criteria** (fail if any one is not met):
> - No waiting: every step completes within its latency budget, with no perceptible stall or spinner.
> - No flicker: layout changes, zoom, and mode switches do not flash, show a white frame, or jump.
> - No zoom error: Fit, 100%, 200%, and free zoom keep correct geometry at every scale, with no stretch or misalignment.
> - No state desync: the directory tree, thumbnails, main view, status bar, and toolbar always agree on the current image and the current directory.

## M44 release-candidate review snapshot (2026-08-15)

- Automated workflow and release gates: **Pass** — final Release build and
  full `.\build.ps1 Test` passed **88/88**, including `workflow_ux_tests`,
  Browse/Compare/export acceptance, `golden_image`, `bench_smoke`,
  `bench_enforce`, and async lifetime gates.
- Data-safety and persistence contracts: **Pass** — command/file-operation
  regressions cover collision, partial batch, rollback failure, Unicode paths,
  cross-volume fallback, and atomic export/persistence paths.
- Native UX review: **MANUAL PENDING / NOT AVAILABLE** — this run did not have
  an executable native Release GUI review environment. Do not treat the
  offscreen workflow tests as a substitute.
- Pending manual scope: Browse/Compare long-session feel, real 1000+ image
  directory, repeated export/cancel, physical ICC, Windows 100/125/150/200%
  DPI and mixed-DPI movement, real two-volume operations, and native Unicode
  dialogs.
- Full evidence: `docs/review/M44_RELEASE_READINESS_STRUCTURAL_CONVERGENCE_2026-08-15.md`.

## M43 fidelity review snapshot (2026-08-15)

- Automated source/display fidelity: **Pass** — `pixelinspector_tests` and
  `compare_acceptance_tests` pass; canonical `build.ps1 Test` is 88/88 in
  three consecutive runs. Coverage includes high-frequency source-vs-LOD
  Inspector RGB, exact 1×1/3×3/5×5/7×7 neighborhoods, LOD replacement
  stability, crop/rotation/adjustment semantics, M42 bounded-memory and
  cancellation/lifetime gates.
- Human UX review: **MANUAL PENDING / NOT AVAILABLE** — native Windows
  mixed-DPI, physical embedded ICC, long-session zoom/flicker feel, and
  hardware RAW16 checks were not performed in the offscreen environment.
- Full evidence: `docs/review/M43_COMPARE_ANALYSIS_FIDELITY_2026-08-15.md`.

---

## Role: UX Review Agent

- **Review only; do not write code.** The job is to check whether every workflow matches user intuition.
- Run this checklist on every PR and report: Pass / Blocked (with the concrete steps and the observed behavior).
- Focus:
  - Whether every button is necessary, and whether every action matches user intuition.
  - Extra clicks, state desync, and unreasonable defaults.
  - Animation, layout, and zoom anomalies.

---

## Workflow 1 — Browse main flow

Open a directory → browse images → wheel to switch → double-click to zoom → restore → close

| Step | Check | Pass criteria |
|---|---|---|
| Open directory | First-screen speed, directory-tree sync | The directory tree highlights and expands the current directory; the first image appears within budget |
| Browse images | Thumbnails, main view | Thumbnails decode on demand with no full-screen block; the main view refreshes immediately |
| Wheel switch | Previous/next latency, state sync | Switching has no wait and no flicker; directory-tree and thumbnail focus follow; the status bar updates path and resolution |
| Double-click zoom | 100% centering, zoom geometry | Double-click zooms to 100% at the mouse point, with correct geometry and no misalignment |
| Restore | Return to Fit | A second double-click or reset returns to Fit, matching the view from before the double-click |
| Close | Resource release | After close there is no GPU or memory leak; opening again leaves no residual state |

## Workflow 2 — Compare main flow

Select two images → Compare → switch (side-by-side / overlay / flicker / Diff) → exit Compare → continue browsing

| Step | Check | Pass criteria |
|---|---|---|
| Select two | Ctrl/Shift multi-select, marquee select | Multi-select is guarded by `blockSignals`, with no re-entrancy loop; selection order is correct |
| Compare | Full screen, default layout, auto-fit window | The first frame is full screen immediately; each image Fits its own pane, fills the viewport with no empty space, and adapts when the window is resized |
| Switch: side-by-side | Side-by-side geometry | Left and right layouts are symmetric; zoom stays in sync |
| Switch: overlay | Overlay alignment | Both images align to the same anchor, with no offset |
| Switch: flicker | Flicker cadence, no tearing | Alternation uses a fixed cadence, with no tearing or stutter |
| Switch: Diff | Difference / heatmap | No tint by default; after “Show difference” is turned on explicitly, the difference is correct and the heatmap colors are readable |
| Exit Compare | Return to browse, consistent state | After exit, the main view and thumbnail selection stay in sync, with no empty cell |
| Continue browsing | No residue | Later switches stay smooth; state from the compare session does not leak into browse mode |

---

## Automation mapping (runs automatically on every `.\build.ps1 Test`)

Checklist items that can be automated are fixed as regression tests and run with the gate. The remaining items are executed manually by the UX Review Agent.

| Checklist scope | Automated test (ctest) | Coverage |
|---|---|---|
| Workflow 1, full flow | `workflow_ux_tests` | Real MainWindow: professional browse toolbar; Folder/Preview sidebar ratio migration; independent Browse and Tab restore; Analysis/Search restore across windows; Enter on a path does not reopen a stale image; cross-volume exact navigation that automatically clears a conflicting directory filter; directory switch clears old state and selects the first image of the new directory once; view mode, fixed size, and adjustable slider state stay in sync. Open directory → onImageOpen → ←/→/Home/End/PageUp/PageDown → first-screen decode ≤ 5 s → Fit/100%/zoom/restore → close |
| Workflow 2, full flow | `workflow_ux_tests` | Real MainWindow full-screen Compare plus real keyboard and mouse events: no Diff tint by default; per-pane Fit plus relative synchronized zoom; B/S/O/K/H modes are mutually exclusive; Space toggles temporary flicker; lock reference; metrics and Inspector stay in sync after threshold, adjustment, and reset; recomputation is coalesced while dragging; Esc exits and browsing continues |
| Compare linked ROI | `m61_roi_workflow_tests` + `workflow_ux_tests` | Real right-click creation for Grid/Split/Overlay/Swipe/Checkerboard; move and eight-direction resize; live mirroring; hidden-panel HUD; TSV copy; cancel / backpressure / stale results; and decoder bounded-region ground truth |
| Compare session restore | `compare_session_tests` | Round-trip of layout, reference image, flicker interval, and difference threshold |
| Compare engine semantics | `compare_workflow_tests` | Select → compare → engine state machine |
| Browse model | `product_browse_tests` | Directory listing / navigation model |
| Image-quality regression | `golden_image` | PSNR / SSIM / pixel-diff against `golden/` |
| Performance budget | `bench_smoke` + `bench_enforce` | Decode / thumbnail / budget ±10% hard gate |
| Manual specials | — (UX Review Agent) | Flicker feel, animation smoothness, zoom-center feel, long browse of 1000 images, memory trend. Native Windows hardware rows are driven by `scripts/manual_acceptance.ps1` (see `docs/acceptance/MANUAL_ACCEPTANCE.md`). |

**Rule**: When a new feature lands and touches any workflow above, extend the matching assertions in `src/test_workflow_ux.cpp` before implementing the feature (test first). An automated test pass is not a checklist pass; the manual specials still require the UX Review Agent’s sign-off.

---

## Categorized checklist

### Browse experience
- [ ] Open directory: first-screen speed meets budget (first thumbnail of a 1000-image directory < 2 s)
- [ ] Wheel browse: previous/next switch with no wait
- [ ] PageUp / PageDown: page turns are correct
- [ ] Home / End: jump to the first and last image is correct

### Compare
- [ ] Ctrl multi-select
- [ ] Shift multi-select
- [ ] Marquee select
- [ ] Compare default layout fills the view and auto-fits the window
- [ ] Synchronized zoom
- [ ] Mode switch (side-by-side / overlay / flicker / Diff) is smooth with no flicker
- [ ] Exit Compare returns to browse with consistent state

### View
- [ ] Fit
- [ ] 100%
- [ ] 200%
- [ ] Zoom center (centered on the mouse point)
- [ ] Pan
- [ ] GIF/WebP: play, pause, restart, previous/next frame, and Frame status are correct
- [ ] Multi-page TIFF: previous/next page navigation and Page status are correct and do not change the Browse file index
- [ ] Frame changes keep zoom and pan and do not re-Fit

### UI
- [ ] Current directory stays in sync (directory tree ↔ main view ↔ status bar)
- [ ] Browse layout: directory and preview stay; after Analysis/Search are hidden, the gallery has no empty gap or jump
- [ ] Directory switch: old thumbnails, preview, and state clear immediately; the first image of the new directory is auto-selected only once
- [ ] Recent files
- [ ] Toolbar
- [ ] Status bar (path / resolution / zoom ratio / frame rate)

### Performance
- [ ] GPU Upload
- [ ] Decode
- [ ] Thumbnail
- [ ] Memory (does not grow during a long browse)

### Stability
- [ ] Open 1000 images in a row
- [ ] Rapid image switching (no crash, no desync)
- [ ] Rapid zoom (no tearing, no zoom error)
- [ ] Long browse (stable memory, no leak)
- [ ] After rapid file switches during animation: no stale frame, no leftover timer, no unbounded background decode

---

## Sign-off

| Item | Value |
|---|---|
| Review revision (commit) | |
| UX Review Agent | |
| Result | □ Pass □ Blocked |
| Blocking items | |

Native Windows MANUAL/BLOCKED hardware rows (DPI, mixed-DPI, ICC, two-volume
FileOps, native dialogs, long paths, long-session RSS, clean-machine install,
UNC, HDR, ROI pointer feel) are recorded with
`scripts/manual_acceptance.ps1`. See `docs/acceptance/MANUAL_ACCEPTANCE.md`.

## M45 qualification notes (2026-08-16)

- Automated coverage includes CommandStack callback re-entry, unresolved and
  ordinary command failures, transfer cancellation safety, and clipboard PNG
  generation/lifecycle. The full local Release gate passed **90/90**.
- Delete is currently labeled **MViewer Recycle Bin** and stages files under the
  per-user application data directory. It is not Windows Explorer's native
  Recycle Bin.
- Native Release GUI evidence for ICC, 100/125/150/200 DPI and mixed-DPI
  monitors, two physical volumes, native file dialogs, and long-session soak
  is still **MANUAL / BLOCKED** in the M45 review until target hardware is
  available.
