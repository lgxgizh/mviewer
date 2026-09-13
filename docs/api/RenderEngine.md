# API — RenderEngine

**Header**: `src/core/render/RenderEngine.h`
**Layer**: core (no Qt *include* — forward-declared `QPainter`/`QRect` in the
rasterization API; see the Qt boundary note and `docs/adr/016-qt-boundary-in-core-headers.md`)

## Purpose
CPU rasterization facade. Produces `ImageData` (Qt-free pixel buffer) for scaled
decode, diff overlay, histogram, heatmap, selection box, and pixel markers. A
`Renderer` backend does the actual pixel work; `RenderEngine` is the singleton
facade plus a `RenderCommand` pipeline.

## Interface (contract)
```cpp
// Global scope: RenderEngine.h opens no namespace, so every type below is global.
enum class RenderCommandType { DrawImage, DrawOverlay, DrawHistogram,
                               DrawSelection, DrawHeatmap, DrawPixelMarker };
enum class RenderInterp { Nearest, Bilinear, Bicubic, Lanczos };

class Renderer {                       // backend interface
public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;
    virtual ImageData scale(const ImageData &src, const RenderSize &, RenderInterp) = 0;
    virtual ImageData overlayDifference(const ImageData &, const ImageData &, double) const = 0;
    virtual ImageData scaleRegion(const ImageData &, const RenderRect &, const RenderSize &, RenderInterp) = 0;
    virtual ImageData heatMap(const ImageData &gray, const RenderRect &) const = 0;
};

class RenderEngine {                  // singleton facade
public:
    static RenderEngine &instance();
    void setBackend(std::unique_ptr<Renderer> r);
    std::string backendName() const;

    ImageData scale(const ImageData &, const RenderSize &, RenderInterp = Bilinear);
    ImageData overlayDifference(const ImageData &, const ImageData &, double = 0.5) const;
    ImageData scaleRegion(const ImageData &, const RenderRect &, const RenderSize &, RenderInterp = Bilinear);
    ImageData heatMap(const ImageData &, const RenderRect &) const;

    // RenderCommand pipeline.
    ImageData executeCommand(const RenderCommand &) const;
    ImageData executeCommands(const std::vector<RenderCommand> &) const;
    static ImageData scaleStatic(...); // backend-independent helpers

    // ── Qt in the API (no Qt include; QPainter/QRect are forward-declared) ──
    // Rasterizes a command onto a caller-supplied QPainter (UI-side compositing).
    void executeCommand(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
};
```

## Qt boundary note (important)
`RenderEngine.h` does **not** include `<QPainter>`: it forward-declares
`class QPainter; class QRect;` (header lines 12-13) and exposes
`executeCommand(QPainter &, const RenderCommand &, const QRect &)` (line 213)
plus private `executeDrawXxx(QPainter &, …)` dispatchers (lines 220-224). So it
is the spot where core rasterizes against a caller-supplied `QPainter`, and the
header is "no Qt include", not "Qt-free" — a caller needs Qt to call that
overload. That is exactly the rule `docs/adr/016-qt-boundary-in-core-headers.md`
states and that the **R5** check in `scripts/architecture_gate.ps1` enforces:
core/domain headers must not *include* Qt, with only
`core/image/QtConvert.h` + `core/image/QtMetadataSemantics.h` allow-listed —
`RenderEngine.h` is **not** one of those two exceptions, because it has no Qt
include to except. RenderEngine is frozen per AGENTS.md, so it is NOT being
refactored out in M12. The remaining GPU/tile pipeline (review P2) is a post-1.0
item.

## Thread-safety
`RenderEngine::instance()` is a Meyers singleton (thread-safe init). `ImageData`
results are value types; the software backend holds no shared mutable state, so
concurrent `scale`/`executeCommand` calls are safe. The `QPainter`-based overload
is intended for the UI thread (caller owns the painter).

## Status
✅ Stable. CPU tile pipeline (review P2) is deferred to post-1.0; no change in M12.
