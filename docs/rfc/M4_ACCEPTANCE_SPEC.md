# M4 Acceptance Test Specification — Professional Viewer Core

**Target suite:** extend `src/core/test_compare.cpp` (currently 139 lines).
**Gate:** all checks below + `test_m3acceptance` must be green before merge.
**Rule:** no implementation until this spec is reviewed and accepted.

---

## A. Compare Engine — synchronization (P0)

### A1. Dual image A|B layout

- `CompareLayout::forCount(2)` → cols=2, rows=1.
- `CompareEngine::setImages({A,B})` → `imageCount()==2`, `isValid()`.

### A2. Synchronized zoom

- Set `setSyncEnabled(true)`; `setScale(2.0)` (200%).
- Assert `cell(0).scale == cell(1).scale == 2.0`.
- Assert `syncTransform().scale == 2.0`.

### A3. Synchronized pan

- `setOffset(10.0, 20.0)`.
- Assert `cell(0).offset == cell(1).offset == (10,20)` (identical x,y).

### A4. Independent transform when sync off

- `setSyncEnabled(false)`; `setCellScale(1, 1.5)`.
- Assert `cell(0).scale != cell(1).scale` (per-cell state independent).

### A5. No UI stall on 50 MP / 8-image grid

- Build 8 × 50 MP frames (synthetic). `setImages(8 paths)`.
- Assert `forCount(8)` grid (4×2) builds; sync transform applied to all cells
  without blocking (timer-based assertion, < budget).

## B. Blink (P0)

### B1. Blink alternation

- `setBlinkIndex(1)` (highlight B).
- After 500 ms tick → `blinkIndex()` returns alternating (-1 / 1) per interval.
- `clearBlink()` → `blinkIndex()==-1`.

## C. Diff (P0)

### C1. abs(A-B) diff map

- Two identical frames → `differenceMap(1,0)` all-zero.
- Two frames differing by constant k → diff map constant k.

### C2. Diff is non-blocking (off UI thread)

- `differenceMap` is submitted to `JobSystem`/`TaskScheduler` (Analysis or
  Background pool); the UI thread returns immediately and receives the
  `DiffResult` via `EventBus`.
- **Verify:** compute diff of two 50 MP frames; assert the calling thread is not
  blocked (wall-clock of the submit call << compute time) and the result arrives
  via the EventBus subscription.
- **Regression guard:** no `DifferenceEngine::differenceMap` call on the UI
  thread in `CompareWorkspace` paint/event handlers.

## D. Pixel Inspector (P0/P1)

### D1. Real-time probe

- `inspectPixel(px,py)` returns `ProbeResult` with per-cell RGB for A and B and
  delta (B−A).
- Identical region → delta == 0 for all channels.

### D2. YUV + coordinates

- `ProbeResult` carries x,y and RGB; YUV derived (assert Y in [0,255], matches
  BT.601 luma of RGB).

## E. Selection / ROI (P1)

### E1. Selection is sole ROI type

- `Analyzer::analyzeRegion(frame, Selection)` compiles and runs; no `QRect` in
  any analyzer signature under `src/core/analyzer/`.
- Region analysis on `Selection{sx,sy,sw,sh}` == full-frame analysis cropped to
  that rectangle (Hist/Mean/Noise).

### E2. ROI reuse

- Same `Selection` passed to Diff + Crop + Analyzer without conversion to QRect.

## F. AnalyzerRegistry stability (P1)

### F1. All built-ins reachable

- `AnalyzerRegistry::instance().create("histogram"|"noise"|"entropy"|"psnr"|
  "rgbmean"|"sharpness"|"ssim")` each returns a non-null analyzer; `analyze`
  returns result text.

### F2. Interface frozen

- `analyze(const ImageFrame&)` and `analyzeRegion(const ImageFrame&,
  const Selection&)` are the only analysis entry points; no new virtuals added
  in M4.

## G. Command unification (P1)

### G1. All commands via CommandStack

- `OpenImageCommand`, `CompareCommand`, `CropCommand`, `RotateCommand` each
  push to `CommandStack`; `undo()`/`redo()` revert/reapply state.
- A composed session (open → compare → crop → rotate) undoes in LIFO order and
  redoes correctly.

## H. Regression guard (mandatory)

### H1. Core pipeline intact

- `test_m3acceptance` green (1000-img ≤100 ms; first thumbnail ≤200 ms).

### H2. No QWidget decode

- Grep of `src/ui` (or equivalent widget layer) shows zero `QImageReader` /
  `Decoder` / `CacheManager` / `fopen` decode calls.
