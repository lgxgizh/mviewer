# MViewer — Public Roadmap

> A fast image viewer/compare/analysis tool for algorithm engineers.
> This is the **user-facing** roadmap. The engineering milestone log lives in
> [`docs/roadmap.md`](roadmap.md); this page is what ships to users.

## Where we are now — **Beta**

MViewer is in **Beta**. Everything below is already built and verified, not
planned:

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
- **Installer** — NSIS `.exe` + portable zip; real UI screenshot in the release.

Supported formats today: **JPEG / PNG / BMP / TIFF** (8/16-bit), Gray / RGBA /
CMYK, plus integrity-edge cases (bad EXIF / bad ICC) handled without crashing.
See [`docs/acceptance/M13.4_real_datasets.md`](acceptance/M13.4_real_datasets.md)
for the verified format matrix.

## Release track

We relabel the product so it reads as a shipping tool, not an R&D line:

| Public version | Theme | Status |
| --- | --- | --- |
| **Beta** (current) | Product loop closed + perf proven + SDK + installer | 🔵 In Beta |
| **1.0** | First non-prerelease: signed installer, docs site, stable plugin ABI | ⬜ Next |
| **1.1** | Large-image depth: 100 MP / tiled decode, compare of huge images | ⬜ Planned |
| **2.0** | GPU-accelerated rendering (see below) + plugin ecosystem | ⬜ Future |

The earlier `v1.0.0-rc` tag was an **internal** pre-release. The real **1.0**
is what this Beta produces.

## What's planned, in priority order

1. **1.0 hardening** — code-signed installer, a docs/README site, and a
   **frozen plugin ABI** (same compiler/Qt per release; see
   [`docs/adr/005-why-plugin-analysis.md`](../adr/005-why-plugin-analysis.md)).
2. **1.1 large images** — disk-LOD decode (Decoder emits reduced-resolution
   bitmaps) so 100 MP / very large JPEG/TIFF open and pan without the full
   bitmap, building on the already-landed Tile Pipeline
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
  (requires MSVC + Qt 6.11). Tests: `build.ps1 Test`.

## How we decide

We follow a **document-driven** cadence (RFC → acceptance → implementation →
review → merge). We finish the vertical product loop before expanding
horizontally, and we keep `domain/`/`core/` Qt-free. We do not add abstraction
"for later." See [`docs/roadmap.md`](roadmap.md) for the engineering detail and
the frozen-build policy.
