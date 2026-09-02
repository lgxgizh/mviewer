# MViewer — Public Roadmap

> A fast image viewer/compare/analysis tool for algorithm engineers.
> This is the **user-facing** roadmap. The engineering milestone log lives in
> [`docs/roadmap.md`](roadmap.md); this page is what ships to users.

## Where we are now — **Beta / 1.0.17 linked ROI measurement (M60)**

MViewer remains in **Beta** on the post-`v1.0.10` hardening line, now released as
  the `1.0.17` patch line. The product
loop below is built and verified; packaging and final human review remain:

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
| **1.0.17** (current line) | Linked source-coordinate ROI measurement, asynchronous source RGB statistics, unequal-dimension guardrails, and M59 color-managed presentation | 🔵 Beta hardening |
| **1.0.16** | Color-managed presentation, canonical ICC/EXIF metadata, monitor-target invalidation, and Browse regression closure | ✅ Previous patch |
| **1.0.15** (current line) | Large-directory query responsiveness, latest-wins filtering, bounded metadata/search updates, and Windows build reproducibility | 🔵 Beta hardening |
| **1.0.14** | Release quality truthfulness, large-source format parity, workflow boundaries, and Windows qualification closure | ✅ Previous patch |
| **1.0.13** | Release quality truthfulness, large-source format parity, workflow boundaries, and Windows qualification closure | ✅ Previous patch |
| **1.0.12** | Release-candidate workflow convergence / native Windows qualification | ✅ Previous patch |
| **1.0.11** | Release-candidate workflow convergence / native Windows qualification | ✅ Previous patch |
| **1.0.10** | Windows UX / Unicode / Compare reliability closure | ✅ Previous patch |
| **1.0.9** | Product loop closed + perf proven + SDK + installer | ✅ Previous patch |
| **1.0** | First non-prerelease: signed installer, docs site, stable plugin ABI | ⬜ Release work |
| **1.1** | Further large-source depth beyond the current JPEG/WIC-TIFF bounded display path | ⬜ Planned |
| **2.0** | GPU-accelerated rendering (see below) + plugin ecosystem | ⬜ Future |

The repository verifies the `v1.0.17` patch line. The remaining 1.0 work is
productization and release review.

The M52 hardening pass also makes release evidence explicit: automated gate
PASS means zero measured hard failures, while advisory complexity warnings are
tracked as accepted baseline debt. Native DPI/ICC/UNC and long-session GUI
feel still require a physical Windows review.

## What's planned, in priority order

1. **1.0 hardening** — code-signed installer, a docs/README site, and a
   **frozen plugin ABI** (same compiler/Qt per release; see
   [`docs/adr/005-why-plugin-analysis.md`](../adr/005-why-plugin-analysis.md)).
2. **1.1 large-source depth** — extend measured format-specific LOD/tile
   coverage beyond the current 100MP JPEG and Windows WIC-TIFF display paths,
   while keeping exact-source analysis separate and bounded
   ([`docs/rfc/M13_TILE_PIPELINE.md`](../rfc/M13_TILE_PIPELINE.md)).
3. **2.0 GPU** — Stage A only (GPU upload + blit via Qt RHI), gated on a
   measured 100 MP deficit. The full staged route is in
   [`docs/rfc/M13_GPU_ROADMAP.md`](../rfc/M13_GPU_ROADMAP.md). **No D3D11/Vulkan
   direct-compositing until the UI boundary is intentionally reopened.**

## Explicitly deferred (not in the current track)

- **Camera RAW — full processing** (NEF/CR2/ARW demosaic) — needs a libraw
  integration (decoder-scope). **Basic opening already shipped (P6):** `RawDecoder`
  extracts the embedded JPEG preview so RAW files display without libraw; full
  demosaic remains a post-1.0 enhancement.
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
