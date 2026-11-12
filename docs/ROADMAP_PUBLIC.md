# MViewer — Public Roadmap

> A fast image viewer/compare/analysis tool for algorithm engineers.
> This is the **user-facing** roadmap. The engineering milestone log lives in
> [`docs/roadmap.md`](roadmap.md); this page is what ships to users.

## Where we are now — **Beta / 1.0.176**

MViewer remains in **Beta**; the current release is **`v1.0.176`**
(2026-11-12, see [`STATUS.md`](../STATUS.md) and [`CHANGELOG.md`](../CHANGELOG.md)).
The product loop below is built and verified. The `1.0.x` numbers are the Beta
patch line; leaving Beta ("1.0 GA") still needs a code-signed installer (no
signing certificate yet), a docs site, a frozen plugin ABI and final human
review:

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
| **1.0.176** (current) | Headerless grayscale `.gray` / `.grey` viewing (ADR 034): 8-bit default, 16-bit and 1-bit, Preferences → Gray and a per-file Gray Parameters dialog, hidden-by-default browse group | 🔵 Beta |
| **1.0.175** | Plain `.raw` follow-up: own **Sensor RAW (.raw)** browse filter group (visible by default) and the RAW review fixes — bit-depth auto-detect instead of wrap, multi-size file names, override follows rename/copy, async pixel readout, faster thumbnails and renders, neutral clipped highlights | ✅ Previous patch |
| **1.0.174** | Plain `.raw` sensor-dump viewing (ADR 033): size from the file name with file-size check and format guessing, Preferences → RAW defaults and a per-file RAW Parameters dialog (bit depth, MIPI/16-bit storage, black/white level, Bayer pattern, white balance, gain, sRGB gamma), sensor values in the pixel readout | ✅ Previous patch |
| **1.0.173** | Project-review pass: complete Simplified Chinese UI, faster non-blocking batch rotate/flip, cache size limits and Clear caches in Preferences, safer plugin loading, RAW metadata fixes, release symbols and build-provenance attestations | ✅ Previous patch |
| **1.0.172** | Restore Details browse zebra (white/gray stripes) on every column, including single-column; remove rating, color-label, pick/reject, and tag UI (shortcuts, filters, metadata editors, badges). Folder Favorites and recently-viewed remain | ✅ Previous patch |
| **1.0.171** | Browse gallery content background: drop the over-broad transparent gallery `::item` QSS that painted gutters black, and stop pinning alternate-background to content bg0; tertiary selectors and flat Details fill stay | ✅ Previous patch |
| **1.0.170** | Tertiary browse row backgrounds: three-column Details no longer shows blue/gray alternating unselected rows that look selected; gallery QSS includes the tertiary pane and Details uses a flat unselected fill like List | ✅ Previous patch |
| **1.0.169** | Triple-browse selection and layout restore: enabling three columns clears parked pending/filter selection so the rightmost pane no longer reselects stale files; turning triple off restores the prior 1- or 2-column layout | ✅ Previous patch |
| **1.0.168** | Three-column directory browsing: independent navigation and sort, separate toolbar/menu action, cross-pane selection and transfer destinations, and saved sessions. One/two-column browsing and Ctrl+3 Details stay unchanged | ✅ Previous patch |
| **1.0.167** | Chrome-like application icon: the frosted tile nearly fills the canvas and the aperture ring nearly fills the tile. Upgrades recreate the desktop and Start-menu shortcuts so the new icon shows right away | ✅ Previous patch |
| **1.0.166** | Larger application icon (the aperture fills the tile) and larger browse chrome: toolbar, controls, icons, body text, menus, sidebar, list rows and the Details header. Compare uses the same control size. Thumbnail size stays a saved setting | ✅ Previous patch |
| **1.0.165** | New application icon: a spectrum aperture ring on a frosted squircle, with simplified artwork at small sizes so it stays legible in Explorer and the taskbar | ✅ Previous patch |
| **1.0.164** | Rotate/flip saves reliably even while the image is still being read (viewer, thumbnails, Windows Search, antivirus), including Chinese paths; rotate/flip disabled with a reason for formats it cannot rewrite; rename/delete/move wait briefly for other readers instead of failing | ✅ Previous patch |
| **1.0.163** | YUV layout picker and extension→format map; no console window on launch (GUI subsystem + VERSIONINFO/manifest); Browse details toggle = Ctrl+3; light-theme menu check/radio/icon ink | ✅ Previous patch |
| **1.0.162** | Raw YUV / Y4M viewing and compare (ADR 032): open `.yuv` / `.y4m` with inferred or saved parameters, step frames, read raw Y/U/V in the pixel inspector and compare two files with per-plane PSNR / SSIM; SIMD conversion and a performance pass (bit-identical output); Nightly Linux ASan disk fix and runner-calibrated benchmark baseline | ✅ Previous patch |
| **1.0.161** | Compare Ctrl+drag local pan (mirrors Ctrl+wheel); light-theme failed-thumb and footer caption contrast plus translatable session status; Nightly quality regression runs `mviewer_bench.exe` only | ✅ Previous patch |
| **1.0.160** | Light-theme readability: unselected file names in the browse list and details views are drawn in dark text again (the stylesheet left them white on a light row), and thumbnail captions use the same readable ink | ✅ Previous patch |
| **1.0.159** | Nightly sanitizer fixes and Windows 11 chrome polish: render engine stays alive past exit so tile workers cannot crash on a null backend, image-plugin formats are range-checked before use, and Mica is title-bar only so menus/path bar/icons no longer ghost and light-theme file names keep their dark text color | ✅ Previous patch |
| **1.0.158** | Reliability and polish pass: zoom limits follow the fit in the viewer and in Compare (huge images no longer jump on zoom-out, tiny ones zoom to 50x), overlay/swipe/checker pan and zoom track the mouse and report the zoom actually drawn, unlinked ROI histograms per pane, split browse keeps each pane's own state, safer saves, deletes, renames, pastes and crash dumps, and translated status texts | ✅ Previous patch |
| **1.0.157** | Steady Space hold: holding Space (or 1–8) in Compare right after a zoom or pan keeps showing the other pane even when a sharper preview finishes loading mid-hold; nightly health publishing retries instead of failing when master moves | ✅ Previous patch |
| **1.0.156** | macOS-style UI redesign: Apple system blue themes, SF Symbols-style icons, unified browse toolbar, Finder-style sidebar and thumbnail cards, Windows 11 chrome with Mica (on by default), compare toolbar segmented modes / capsule menus, and polished dialogs & menus | ✅ Previous patch |
| **1.0.155** | Batch Rename: Ctrl+Shift+B loads every image of the current folder, previews current → new names live and renames the originals in place (swaps safe, a replaced file goes to the Recycle Bin); the crowded batch processing options are gone | ✅ Previous patch |
| **1.0.154** | Space shows the truth: holding Space (or 1–8) in Compare shows the other pane exactly as it is zoomed and panned, the ROI box and statistics chip follow the shown image, a split hold keeps both halves visible, Ctrl+C while held copies what is on screen, and the help explains what Ctrl+C copies in each mode | ✅ Previous patch |
| **1.0.153** | Clipboard polish: Compare Ctrl+C / Ctrl+S captures the on-screen view including ROI statistics chips; browse Ctrl+C puts the selected image files on the clipboard so Explorer Ctrl+V pastes copies | ✅ Previous patch |
| **1.0.152** | Calm previews: switching images in a folder of large photos shows the new image's name at once (never the previous photo standing in), the real source size is known from the thumbnail pass, tiny thumbnails wait briefly for the sharp preview, the stats line no longer jumps, and the centre viewer no longer holds the previous image while the next one loads | ✅ Previous patch |
| **1.0.151** | Smooth previews and Compare polish: the browse preview keeps a stable frame (no "small then grows" flicker, EXIF thumbnail bars cropped, sharp on HiDPI), the viewer's provisional image lands exactly where the full image will, Compare overlays and histograms follow the temporarily shown image, split browse mirrors the current folder, and a deep-zoom Compare pane no longer stays soft | ✅ Previous patch |
| **1.0.150** | Browse UX: multi-select type filter (RAW hidden by default, "N files hidden" hint with one-click Show), union Compare across split panes, Compare pane header and status as overlays so every pane has the same viewport, oriented Resolution, type filter keeps selection and scroll | ✅ Previous patch |
| **1.0.149** | Stability: sanitizer-clean Nightly (ASan/LSan on Windows clang-cl and Linux GCC, UBSan) — fixes a cache use-after-free at exit, a scheduler handle race, and Qt slot/STL undefined-behavior traps | ✅ Previous patch |
| **1.0.148** | Design review batches: split-browse copy/move to the other pane, RAW source badge + full decode, viewer-scoped Delete/F2 and lossless JPEG rotate, analysis panel → `F7`, immediate language switch, new user guide chapters | ✅ Previous patch |
| **1.0.147** | Split-browse routing fixes, Compare per-pane display state machine (no stuck Loading), end-to-end view-switch benchmarks, Flip V → `Ctrl`+`Alt`+`V`, thumbnail reset → `Ctrl`+`Alt`+`9` (`Ctrl`+`0` always fits), refreshed translations | ✅ Previous patch |
| **1.0.146** | Split Browse View parallel layout: independent column sort, view-mode sync, and secondary thumbnail pipeline consumer routing | ✅ Previous patch |
| **1.0.145** | Opt-in Pixel Grid; Compare high-zoom mesh eliminated unless inspection lattice is enabled | ✅ Previous patch |
| **1.0.144** | Split Browse View: independent dual-pane gallery (Primary / Secondary) with isolated directory monitoring & thumbnail pipelines, cross-pane 2–8 natural image selection, 3-tier deterministic pairing sort, and caption disambiguation | ✅ Previous patch |
| **1.0.143** | Smooth fullscreen transitions: eliminate black flash on double-click open and small-image artifact on ESC exit; 16-bit analysis pathway; hot-switch language without restart; USM Space hold | ✅ Previous patch |
| **1.0.142** | Review hardening: complexity splits, RenderEngine namespace, build portability, LibRaw cancel, DiskCache threshold, unified luma, acceptance wizard, orderly test exit | ✅ Previous patch |
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
| **1.0 GA** | Leave Beta: code-signed installer, docs site, frozen plugin ABI (the `1.0.x` patches above ship as Beta) | ⬜ Blocked on a signing certificate |
| **1.1** | Further large-source depth beyond the current JPEG/WIC-TIFF bounded display path | ⬜ Planned |
| **2.0** | GPU-accelerated rendering (see below) + plugin ecosystem | ⬜ Future |

The repository verifies the `v1.0.176` patch line. The remaining 1.0 GA work is
productization (signing, ABI freeze) and release review.

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
