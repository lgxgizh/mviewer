# MViewer — Public Roadmap

> A fast image viewer/compare/analysis tool for algorithm engineers.
> This is the **user-facing** roadmap. The engineering milestone log lives in
> [`docs/roadmap.md`](roadmap.md); this page is what ships to users.

## Where we are now — **Beta / 1.0.142**

MViewer remains in **Beta**; the current release is **`v1.0.142`**
(2026-10-20, see [`STATUS.md`](../STATUS.md) and [`CHANGELOG.md`](../CHANGELOG.md)).
The product loop below is built and verified; the non-prerelease 1.0 work
(signed installer, docs site, frozen plugin ABI) and final human review remain:

- **Browse** — open a 1000-image directory without UI freeze; thumbnails stream
  in; first thumbnail < ~35 ms (cold).
- **Compare** — 2–8 images side by side with locked zoom/pan/scroll/ROI; diff
  heatmap + blink.
- **Analyze** — histogram, RGB mean, PSNR, SSIM, sharpness, noise, entropy over
  the full image or a selected region; results surfaced generically (no custom
  UI per analyzer).
- **Plugins** — drop an analyzer `.dll` next to the app; it self-registers and
  appears in the Analysis panel. SDK + reference plugin documented in
  [`docs/sdk/PLUGIN_SDK.md`](sdk/PLUGIN_SDK.md).
- **Export** — compare report (JSON/CSV) + diff PNG.
- **Workspace** — persist open directories, ROI, and per-image analysis to disk
  and restore.
- **Multi-frame / multi-page** — animated GIF/WebP play with explicit frame
  status and bounded prefetch; multi-page TIFF supports page navigation. The
  gallery remains one item per source, and Compare/workspace state names the
  selected frame/page.
- **Large sources** — 100MP JPEG and 100MP-class TIFF now use bounded
  source-backed display on Windows; exact-source analysis remains separate.
- **Installer** — NSIS `.exe` + portable zip; real UI screenshot in the release.
- **Camera RAW** — CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF/… display through the
  embedded JPEG preview; **LibRaw 0.21.4** (LGPL-2.1 / CDDL-1.0, integrated in
  1.0.77) supplies half-size demosaic when the preview is too short and full
  demosaic only when no preview exists.
- **Compare tools** — per-pane local zoom (`Ctrl`+wheel, 1.0.139), per-pane USM
  sharpening and the USM before/after dialog (1.0.133–1.0.136, 1.0.140),
  cursor-aware `Space` pair-hold.
- **Languages** — English source text with a Simplified Chinese translation;
  **Tools → Preferences → UI language** (Follow system / English / Simplified
  Chinese, 1.0.138).
- **Updates** — Help → Check for Updates plus an optional silent startup check
  (**Automatically check for updates on startup**, 1.0.140).

Supported formats today: **JPEG / PNG / BMP / TIFF** (8/16-bit), plus
**GIF animation** and **animated WebP when the deployed Qt qwebp plugin is
available**. Gray / RGBA / CMYK and integrity-edge cases (bad EXIF / bad ICC)
remain handled without crashing. TIFF multi-page support and GIF/WebP runtime
capability are verified by the M57 real-plugin test; a missing plugin is a
diagnostic/deployment failure, not silently treated as full support.
See [`docs/acceptance/M13.4_real_datasets.md`](acceptance/M13.4_real_datasets.md)
for the verified format matrix.

## Release track

We relabel the product so it reads as a shipping tool, not an R&D line:

| Public version | Theme | Status |
| --- | --- | --- |
| **1.0.142** (current) | Review hardening: complexity splits, RenderEngine namespace, build portability, LibRaw cancel, DiskCache threshold, unified luma, acceptance wizard, orderly test exit | 🔵 Beta |
| **1.0.141** | Compare stuck "Loading" / low-res mosaic after a pair switch fixed | ✅ Previous patch |
| **1.0.140** | Compare mosaic fix, automatic-update-check preference, USM dialog maximize + `Space` pair-hold | ✅ Previous patch |
| **1.0.139** | Compare local zoom (`Ctrl`+wheel), paste images into the browsed folder, Open Folder | ✅ Previous patch |
| **1.0.138** | English source language + Simplified Chinese localization, UI language setting | ✅ Previous patch |
| **1.0.137** | 60 static-audit fixes | ✅ Previous patch |
| **1.0.133–1.0.136** | USM sharpen before/after compare and export, per-pane USM in Compare | ✅ Previous patches |
| **1.0.77** | Viewport region tiles, GPU Stage A tile residency, LibRaw half/full demosaic | ✅ Previous patch |
| **1.0.31–1.0.132** | Performance, UX and reliability patch line (per-version detail in `CHANGELOG.md`) | ✅ Previous patches |
| **1.0.30** | Review hardening: bounded file/settings input, deterministic delivery, teardown evidence | ✅ Previous patch |
| **1.0.9–1.0.29** | Product loop, Windows qualification, color management, linked ROI, Compare inspection chrome | ✅ Previous patches |
| **1.0** | First non-prerelease: signed installer, docs site, stable plugin ABI | ⬜ Release work |
| **1.1** | Further large-source depth beyond the current JPEG/WIC-TIFF bounded display path | ⬜ Planned |
| **2.0** | GPU-accelerated rendering (see below) + plugin ecosystem | ⬜ Future |

The repository verifies the `v1.0.142` patch line. The remaining 1.0 work is
productization and release review.

The M52 hardening pass also makes release evidence explicit: automated gate
PASS means zero measured hard failures, while advisory complexity warnings are
tracked as accepted baseline debt. Native DPI/ICC/UNC and long-session GUI
feel still require a physical Windows review.

## What's planned, in priority order

1. **1.0 hardening** — code-signed installer, a docs/README site, and a
   **frozen plugin ABI** (same compiler/Qt per release; see
   [`docs/adr/005-why-plugin-analysis.md`](adr/005-why-plugin-analysis.md)).
2. **1.1 large-source depth** — extend measured format-specific LOD/tile
   coverage beyond the current 100MP JPEG and Windows WIC-TIFF display paths,
   while keeping exact-source analysis separate and bounded
   ([`docs/rfc/M13_TILE_PIPELINE.md`](rfc/M13_TILE_PIPELINE.md)).
3. **2.0 GPU** — Stage A only (GPU upload + blit via Qt RHI), gated on a
   measured 100 MP deficit. The full staged route is in
   [`docs/rfc/M13_GPU_ROADMAP.md`](rfc/M13_GPU_ROADMAP.md). **No D3D11/Vulkan
   direct-compositing until the UI boundary is intentionally reopened.**

## Explicitly deferred (not in the current track)

- **Camera RAW — beyond LibRaw's internal decoders** — LibRaw 0.21.4 ships
  without external libjpeg/zlib/lcms, so some DNG deflate/lossy files still fail
  to open, and a build with `MVIEWER_WITH_LIBRAW=OFF` falls back to the embedded
  preview only (see "Known gaps" in [`STATUS.md`](../STATUS.md)).
- **GPU Stage C/D** (Direct2D/D3D11 direct compositing, Vulkan) — deferred per
  the frozen `UI = Qt Widgets` boundary and the "CPU tile is enough for v1"
  guidance.
- **Python / Lua / AI / OpenCV plugins** — the `Analyzer` ABI is the seam;
  language bindings are a post-1.0 concern.

## Build & install

- Windows installer + portable zip: see the latest GitHub release.
- Build from source: `powershell -ExecutionPolicy Bypass -File build.ps1 Release`
  (requires MSVC + Qt 6.8 minimum; development qualification uses Qt 6.10.x).
  Tests: `build.ps1 Test`.

## How we decide

We follow a **document-driven** cadence (RFC → acceptance → implementation →
review → merge). We finish the vertical product loop before expanding
horizontally, and we keep `domain/`/`core/` Qt-free. We do not add abstraction
"for later." See [`docs/roadmap.md`](roadmap.md) for the engineering detail and
the frozen-build policy.
