# API — RenderEngine

**Header**: `src/core/render/RenderEngine.h`
**Layer**: core (mostly Qt-free; **sanctioned** `QPainter` boundary — see M12.5 §5.1)

## Purpose
CPU rasterization facade. Produces `ImageData` (Qt-free pixel buffer) for scaled
decode, diff overlay, histogram, heatmap, selection box, and pixel markers. A
`Renderer` backend does the actual pixel work; `RenderEngine` is the singleton
facade plus a `RenderCommand` pipeline.

## Interface (contract)
```cpp
namespace mviewer::core {

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

    // ── SANCTIONED Qt boundary (M12.5 §5.1) ──
    // Rasterizes a command onto a caller-supplied QPainter (UI-side compositing).
    void executeCommand(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
};

} // namespace mviewer::core
```

## Qt boundary note (important)
`RenderEngine.h` includes `<QPainter>` and exposes `executeCommand(QPainter&, …)`
plus private `executeDrawXxx(QPainter&, …)` dispatchers. This is the **single
deliberate spot** where the Qt-free core does CPU rasterization against a
`QPainter`. Everything else in the header operates on `ImageData`. This is
**sanctioned** (documented in M12.5 §5.1); RenderEngine is frozen per AGENTS.md,
so it is NOT being refactored out in M12. The remaining GPU/tile pipeline (review
P2) is a post-1.0 item.

## Thread-safety
`RenderEngine::instance()` is a Meyers singleton (thread-safe init). `ImageData`
results are value types; the software backend holds no shared mutable state, so
concurrent `scale`/`executeCommand` calls are safe. The `QPainter`-based overload
is intended for the UI thread (caller owns the painter).

## Status
✅ Stable. CPU tile pipeline (review P2) is deferred to post-1.0; no change in M12.
