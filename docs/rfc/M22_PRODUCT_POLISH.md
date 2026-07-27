# RFC — M22 Product Polish (high-value items from 2026-07-27 review)

**Status:** DRAFT for review (RFC-first). Distills the four high-value product
polish items surfaced in the `2026-07-27` codebase review into implementable
proposals. Each item has a companion ADR under `docs/adr/M22_*`.

**Author:** OpenCode (writer), per commander direction.
**Date:** 2026-07-27
**Scope guard:** implementations stay *outside* the frozen modules
(`CacheManager`, `Scheduler`, `DecoderRegistry` internals, Build, CI, Plugin
Framework, Workspace base, Performance Gate). Adding a decoder / format through
the existing seam is allowed; refactoring the seam is not.

---

## F1 — Centralized Preferences Dialog

**Why:** Today every configurable option lives in scattered menus / context
menus / per-dialog controls, and is persisted via `QSettings` across
`mainwindow`, `imageviewer`, `exportcommand`, `pluginsettings`. Pro users expect
one place to tune the app. A tabbed Preferences dialog also becomes the natural
home for the new toggles introduced by F2/F3/F4.

**What:**
- New `PreferencesDialog` (Qt Widgets) with tabs: 常规 / 视图 / 对比 / 分析 / 导出 / 插件 / 高级.
- Backed entirely by existing `QSettings` keys where they exist; a small set of
  new keys for genuinely new options (e.g. `autoAlignBeforeDiff`,
  `defaultAnalysisOverlay`, `confirmDelete`).
- Exposes at minimum: 默认排序/视图模式、缩略图默认尺寸、幻灯片间隔、确认删除开关、GPU 开关（`MVIEWER_GPU` 仅作提示，不改启动语义）、F3 对齐开关、F4 默认叠加层。

**Acceptance:** open Preferences, change a value, close, reopen → value
persists across sessions; applied live where feasible (view/compare/analysis).

**Constraints:** No new persistence layer; only reads/writes `QSettings`.
Does not modify frozen modules.

---

## F2 — Broaden Decode Format Coverage

**Why:** `QtDecoder::kExtensions` is hard-coded to 6 formats
(`jpg/jpeg/bmp/png/tif/tiff`). Qt itself can decode more (WebP, GIF, and — when
plugins ship — HEIF/AVIF) via `QImageReader::supportedImageFormats()`. Image
algorithm engineers routinely handle WebP/HEIF/AVIF/HDR. The current list
silently drops anything else.

**What:**
- `QtDecoder::extensions()` / `canDecode()` derive the supported set from
  `QImageReader::supportedImageFormats()` at construction (lower-cased), instead
  of the static 6-entry list. `RawDecoder` stays registered first and keeps
  owning RAW extensions, so RAW preview behavior is unchanged.
- Result: every format Qt can actually decode gains first-class decoding +
  M6 metadata (orientation/color-space/container) for free.
- Long-term (separate RFC, *not* this change): dedicated `IDecoder`s for
  EXR / HEIF / AVIF when Qt plugins are unavailable.

**Acceptance:** a WebP (and, when the Qt plugin is present, HEIF/AVIF) file
opens with correct dimensions/orientation/color-space; `supportedExtensions()`
lists them; a unit test asserts the set is a superset of the historical 6.

**Constraints:** `DecoderRegistry` internals untouched — only the *contents*
of `QtDecoder`'s claim list change, through its existing `extensions()` API.
`QtFallbackDecoder` remains the last safety net.

---

## F3 — Compare Auto-Alignment before Diff Metrics

**Why:** Algorithm engineers compare two renders of the same scene that often
differ by a small translation / integer-pixel shift (cropping, resampling,
mis-aligned pipelines). Computing PSNR/SSIM/diff on un-aligned images is
dominated by the mis-registration, not by real signal error.

**What:**
- New `core/compare/Aligner` (Qt-free core, unit-testable): estimates a 2D
  translation (and, in a later phase, optionally affine) that best registers
  image B to image A, using phase correlation (FFT-free, integer-search over a
  bounded window via normalized cross-correlation / SAD on luminance).
- `CompareSession` / diff-metric path invokes the aligner when the
  `autoAlignBeforeDiff` preference is on, aligns B to A, then feeds the aligned
  frames to PSNR/SSIM/diff.
- UI shows the detected offset and a per-metric "aligned" flag.

**Acceptance:** synthetic A vs `shift(A, dx, dy)` recovers `(dx,dy)` exactly;
PSNR of aligned pair → ∞ (or ≥ threshold); unit test on generated data.

**Constraints:** new `core/compare` module (not frozen). No change to
`Analyzer` plugin interface or `DecoderRegistry`. Alignment is *optional and
off by default* to preserve current deterministic behavior.

---

## F4 — Analysis Overlays (zebra / false-color / scope)

**Why:** The analysis suite is metric-rich (histogram, PSNR, SSIM, MTF,
dead-pixel, ColorChecker ΔE, …) but offers no *at-a-glance visual* judgement
tools that ISP / color engineers rely on while browsing.

**What:**
- **Zebra:** overlay marking over-exposed (≥ threshold, default 98%) and
  under-exposed (≤ threshold, default 2%) pixels as diagonal hatching. Toggle +
  threshold in Preferences (analysis tab).
- **False-color:** map luminance (or a chosen channel) to a perceptual colormap
  (inferno/jet/turbo) as a viewer overlay mode, for spotting gradients/clipping.
- **Waveform / Vectorscope:** scope widgets in `AnalysisPanel` (or a dock) —
  waveform plots per-channel luma, vectorscope plots chroma vectors; both fed
  from the current frame / ROI.

**Acceptance:** zebra toggles on/off and tracks the threshold live; false-color
renders a stable colormap; waveform/vectorscope draw from the loaded frame and
update on image change; all off by default.

**Constraints:** overlay rendering reuses the existing `RenderEngine`
`DrawOverlay` command path (`RenderCommandType::DrawOverlay` already exists in
`RenderEngine.h`); scope widgets are plain `QWidget`s in the UI layer. No frozen
module touched.

---

## Subtraction check

All four proposals add *features at the seams*, not architecture. None modify
frozen modules. F1/F4 are pure UI + QSettings; F2 is a one-file decoder change;
F3 is a new self-contained core module integrated optionally. No scope expansion
beyond what the commander delegated.

*Cross-refs: `docs/adr/M22_PREFERENCES.md`, `docs/adr/M22_FORMAT_COVERAGE.md`,
`docs/adr/M22_COMPARE_ALIGNMENT.md`, `docs/adr/M22_ANALYSIS_OVERLAYS.md`,
`src/core/image/decoder/QtDecoder.cpp`, `src/core/analyzer/`, `src/imageviewer.cpp`.*
