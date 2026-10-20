# API — RenderEngine

**Header**: `src/core/render/RenderEngine.h`
**Namespace**: `mviewer::core` (global `using` aliases keep the old unqualified names working)
**Layer**: core — Qt-free header (no Qt include and no Qt type in any signature;
see `docs/adr/016-qt-boundary-in-core-headers.md`, 2026-10 addendum)

## Purpose

CPU rasterization facade. Produces `ImageData` (Qt-free pixel buffer) for scaled
decode, diff overlay, histogram, heatmap, selection box, and pixel markers. A
`Renderer` backend does the actual pixel work; `RenderEngine` is the singleton
facade plus a `RenderCommand` pipeline. Compositing a command onto a `QPainter`
is a UI concern and lives in the UI render adapter (below).

## Interface (contract)

```cpp
namespace mviewer::core {
enum class RenderCommandType { DrawImage, DrawOverlay, DrawHistogram,
                               DrawSelection, DrawHeatmap, DrawPixelMarker };
enum class RenderInterp { Nearest, Bilinear, Bicubic, Lanczos };
struct RenderSize; struct RenderRect; struct RenderCommand;

class Renderer {                       // backend interface
public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;
    virtual ImageData scale(const ImageData &src, const RenderSize &, RenderInterp) = 0;
    virtual ImageData overlayDifference(const ImageData &, const ImageData &, double) const = 0;
    virtual ImageData scaleRegion(const ImageData &, const RenderRect &, const RenderSize &, RenderInterp) = 0;
    virtual ImageData heatMap(const ImageData &gray, const RenderRect &) const = 0;
};
class SoftwareRenderer : public Renderer { /* default CPU backend */ };

class RenderEngine {                  // singleton facade
public:
    static RenderEngine &instance();
    void setBackend(std::unique_ptr<Renderer> r);
    std::string backendName() const;

    ImageData scale(const ImageData &, const RenderSize &, RenderInterp = Bilinear);
    ImageData overlayDifference(const ImageData &, const ImageData &, double = 0.5) const;
    ImageData scaleRegion(const ImageData &, const RenderRect &, const RenderSize &, RenderInterp = Bilinear);
    ImageData heatMap(const ImageData &, const RenderRect &) const;

    // RenderCommand pipeline (ImageData only).
    ImageData executeCommand(const RenderCommand &) const;
    ImageData executeCommand(const RenderCommand &, const ImageData &buffer) const;
    ImageData executeCommands(const std::vector<RenderCommand> &) const;
    static ImageData scaleStatic(...); // backend-independent helpers
};
} // namespace mviewer::core

// Compatibility aliases (existing callers keep compiling):
using RenderCommandType = mviewer::core::RenderCommandType;
using RenderInterp = mviewer::core::RenderInterp;
using RenderSize = mviewer::core::RenderSize;
using RenderRect = mviewer::core::RenderRect;
using Renderer = mviewer::core::Renderer;
using SoftwareRenderer = mviewer::core::SoftwareRenderer;
using RenderCommand = mviewer::core::RenderCommand;
using RenderEngine = mviewer::core::RenderEngine;
```

## UI render adapter (QPainter compositing)

```cpp
// src/rendercommandpainter.h (UI layer, mviewer_ui)
namespace mviewer::ui {
void paintRenderCommand(QPainter &painter, const mviewer::core::RenderCommand &cmd,
                        const QRect &viewport);
}
```

Draws one command onto a caller-supplied painter, clipped to `viewport` (the
cell's local rectangle). `DrawImage` / `DrawOverlay` scale through
`RenderEngine::instance().scale`, i.e. the active backend.
`SmoothPixmapTransform` is enabled for heatmaps when `cmd.interp != 0`. This
replaces the former `RenderEngine::executeCommand(QPainter &, …)` overload,
which made the core header depend on Qt types.

## Qt boundary note

`RenderEngine.h` neither includes Qt nor names a Qt type, so it satisfies the
core-header rule that the **R5** check in `scripts/architecture_gate.ps1`
enforces (only `core/image/QtConvert.h` + `core/image/QtMetadataSemantics.h`
may include Qt). `RenderEngine.cpp` still uses Qt internally (`QImage`
resampling), which ADR-016 allows for core `.cpp` bodies.

## Thread-safety

`RenderEngine::instance()` is a Meyers singleton (thread-safe init). `ImageData`
results are value types; the software backend holds no shared mutable state, so
concurrent `scale`/`executeCommand` calls are safe. `paintRenderCommand` is
intended for the UI thread (caller owns the painter).

## Status

✅ Stable. CPU tile pipeline (review P2) is deferred to post-1.0.
