# RenderEngine Specification

## Module

RenderEngine + Renderer interface + SoftwareRenderer

## Purpose

RenderEngine is the UI-independent facade over image rendering. It supports a pluggable backend (Renderer interface), defaulting to SoftwareRenderer (Qt-backed). UI code submits RenderCommand batches; the engine dispatches to the current backend.

## API

```cpp
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;
    virtual ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode) = 0;
    virtual ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha) = 0;
    virtual ImageData scaleRegion(const ImageData& src, const RenderRect& region, const RenderSize& target, RenderInterp mode) = 0;
};

class RenderEngine {
public:
    static RenderEngine& instance();
    void setBackend(std::unique_ptr<Renderer> r);
    ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode = RenderInterp::Bilinear);
    ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha = 0.5);
    ImageData scaleRegion(const ImageData& src, const RenderRect& region, const RenderSize& target, RenderInterp mode = RenderInterp::Bilinear);
};
```

## Input / Output

| Method | Input | Output |
| -------- | ------- | -------- |
| `scale` | `src`, `target.size`, `interp` | Scaled ImageData (RGB24) |
| `overlayDifference` | `base`, `diff`, `alpha∈[0,1]` | Blended ImageData (RGB24) |
| `scaleRegion` | `src.region`, `target.size`, `interp` | Scaled sub-image |

## Ownership

- RenderEngine **owns** the current Renderer backend.
- Caller provides ImageData by const reference; output is a new ImageData (value semantics).
- ImageFrame::renderCache stores results (weak-cache semantics; UI may discard at any time).

## Thread Safety

| Thread | Use |
| -------- | ----- |
| UI thread | Scale/overlay results dispatched to Qt pixmap |
| Background | Pre-render for comparison (future) |
| Backend | Single-threaded (Qt QImage paint device) |

## Memory

| Operation | Dominant Allocation |
| ----------- | --------------------- |
| `scale` | `target.w * target.h * 3` bytes (output ImageData) |
| `overlayDifference` | same as base + same as diff |
| Cache entries in ImageFrame | Bounded by LRU eviction in CacheManager (Viewer pool) |

## Performance

| Scenario | Budget |
| ---------- | -------- |
| `scale(1920→800)` | <15 ms |
| `overlayDifference(1080p)` | <5 ms |
| `scaleRegion(ROI 200x200→400x400)` | <3 ms |

## Errors

| Error | Cause | Recovery |
| ------- | ------- | ---------- |
| null/empty source | Invalid input | Return null ImageData |
| zero/negative target | Invalid size | Return null ImageData |
| backend failure | Internal Qt error | Log, return null |

## Examples

```cpp
// Scale to fit viewport
RenderSize target{cellW, cellH};
ImageData scaled = RenderEngine::instance().scale(frame.pixels(), target);

// Overlay diff at 50% alpha
ImageData blended = RenderEngine::instance().overlayDifference(base, diff, 0.5);

// Scale a ROI
RenderRect roi = {100, 100, 400, 400};
ImageData zoomed = RenderEngine::instance().scaleRegion(frame.pixels(), roi, {800, 800});
```

## Unit Tests

```cpp
TEST(RenderEngine, ScaleNullInput) {
    ImageData out = RenderEngine::instance().scale(ImageData(), {100, 100});
    EXPECT_TRUE(out.isNull());
}

TEST(RenderEngine, Scale100to50) {
    ImageData data = makeTestData(100, 100);
    ImageData out = RenderEngine::instance().scale(data, {50, 50});
    EXPECT_EQ(out.width, 50);
    EXPECT_EQ(out.height, 50);
}

TEST(RenderEngine, OverlayNullInput) {
    ImageData out = RenderEngine::instance().overlayDifference(ImageData(), ImageData(), 0.5);
    EXPECT_TRUE(out.isNull());
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenario `Render::scale(1920x1080→1280x720)`.

## Future Extension

- D2DRenderer (Direct2D, Windows-only)
- OpenGLRenderer (cross-platform)
- VulkanRenderer (cross-platform, high-perf)
- MetalRenderer (macOS)
- GPU-accelerated diff/heatmap via compute shaders
