# RenderCommand Specification

## Module
RenderCommand + RenderCommandType + RenderInterp + RenderSize + RenderRect + Renderer + SoftwareRenderer + RenderEngine

## Purpose
RenderCommand is a flat-struct command pattern for composable UI draw operations. Each command carries its data inline (srcImage, overlayImage, histogram bins, rect, color, alpha). 5 operations: DrawImage, DrawOverlay, DrawHistogram, DrawSelection, DrawPixelMarker. The RenderEngine facade dispatches to a pluggable Renderer backend (default: SoftwareRenderer). Flat struct (not union) because ImageData has a non-trivial constructor.

## API

```cpp
enum class RenderCommandType : uint8_t {
    DrawImage,        // scale srcImage → targetSize with interp
    DrawOverlay,      // blend overlayImage over current buffer at alpha
    DrawHistogram,    // draw bar chart from histData[0..histCount] into rect
    DrawSelection,    // draw marquee rect with rgba color
    DrawPixelMarker,  // draw crosshair marker at (x,y) with rgba color
};

enum class RenderInterp : int { Nearest, Bilinear, Bicubic, Lanczos };

struct RenderSize {
    int width = 0;
    int height = 0;
    bool isValid() const;
};

struct RenderRect {
    int x = 0, y = 0, width = 0, height = 0;
    bool isValid() const;
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;
    virtual ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode) = 0;
    virtual ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha) = 0;
    virtual ImageData scaleRegion(const ImageData& src, const RenderRect& region, const RenderSize& target, RenderInterp mode) = 0;
};

class SoftwareRenderer : public Renderer {
public:
    std::string backendName() const override { return "software"; }
    ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode) override;
    ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha) override;
    ImageData scaleRegion(const ImageData& src, const RenderRect& region, const RenderSize& target, RenderInterp mode) override;
};

struct RenderCommand {
    RenderCommandType type;
    RenderSize    targetSize;
    RenderRect    rect;
    int           rgba = 0;
    double        alpha = 1.0;
    int           interp = 0;
    std::array<int, 256> histData{};
    int           histCount = 0;
    ImageData     srcImage;
    ImageData     overlayImage;

    static RenderCommand drawImage(const ImageData& img, const RenderSize& tgt, RenderInterp m);
    static RenderCommand drawOverlay(const ImageData& img, double a);
    static RenderCommand drawHistogram(const int* bins, int n, const RenderRect& r);
    static RenderCommand drawSelection(const RenderRect& r, int rgb);
    static RenderCommand drawPixelMarker(int x, int y, int rgb);
};

class RenderEngine {
public:
    static RenderEngine& instance();
    void setBackend(std::unique_ptr< Renderer> r);

    ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode = RenderInterp::Bilinear);
    ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha = 0.5);
    ImageData scaleRegion(const ImageData& src, const RenderRect& region, const RenderSize& target, RenderInterp mode = RenderInterp::Bilinear);

    // Static compat API (uses default software backend)
    static ImageData scaleStatic(const ImageData& src, const RenderSize& target, RenderInterp mode = RenderInterp::Bilinear);
    static ImageData overlayDifferenceStatic(const ImageData& base, const ImageData& diff, double alpha = 0.5);
    static ImageData scaleRegionStatic(const ImageData& src, const RenderRect& region, const RenderSize& target, RenderInterp mode = RenderInterp::Bilinear);
};
```

## Input

| Factory Parameter | Type | Constraints | Default |
|-------------------|------|-------------|---------|
| `img` | `const ImageData&` | Valid pixels for DrawImage/DrawOverlay | — |
| `tgt` | `const RenderSize&` | width>0, height>0 | — |
| `m` | `RenderInterp` | — | — |
| `a` | `double` | 0.0–1.0 | — |
| `bins` | `const int*` | Non-null if n>0 | — |
| `n` | `int` | ≤256 | — |
| `r` | `const RenderRect&` | — | — |
| `rgb` | `int` | Packed 0xRRGGBB | — |
| `x, y` | `int` | Pixel coords | — |

## Output

| Factory | Return | Content |
|---------|--------|---------|
| `drawImage` | `RenderCommand` | type=DrawImage, srcImage=img, targetSize=tgt, interp=m |
| `drawOverlay` | `RenderCommand` | type=DrawOverlay, overlayImage=img, alpha=a |
| `drawHistogram` | `RenderCommand` | type=DrawHistogram, histData[0..n]=bins, rect=r, histCount=n |
| `drawSelection` | `RenderCommand` | type=DrawSelection, rect=r, rgba=rgb |
| `drawPixelMarker` | `RenderCommand` | type=DrawPixelMarker, targetSize={x,y}, rgba=rgb |

## Ownership

- RenderCommand is a value-type (all inline data); no heap except ImageData (value, refcounted internally).
- RenderEngine **owns** the current Renderer backend via `unique_ptr<Renderer>`.
- RenderEngine::instance() is a singleton; backend can be swapped at runtime.

## Thread Safety

| Method | Thread | Mechanism |
|--------|--------|-----------|
| `scale/overlayDifference/scaleRegion` | UI thread typically | Backend dispatch; Qt paint device not thread-safe |
| `setBackend` | UI thread | Atomic swap |
| `scaleStatic/overlayDifferenceStatic/scaleRegionStatic` | Any thread | Stateless w/ static software backend |

## Memory

| Operation | Dominant Allocation |
|-----------|---------------------|
| `scale` | `target.w * target.h * 3` bytes (output ImageData) |
| `overlayDifference` | same as base + same as diff |
| `scaleRegion` | `target.w * target.h * 3` bytes |
| `RenderCommand` | ~2.5 KB (256-int histData array + 2 ImageData) |

## Performance

| Scenario | Budget | Baseline |
|----------|--------|----------|
| `scale(1920→800)` | <15 ms | ~13 ms |
| `overlayDifference(1080p)` | <5 ms | ~3 ms |
| `scaleRegion(200×200→400×400)` | <3 ms | ~1.5 ms |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| null/empty source | Invalid input | Return null ImageData |
| zero/negative target | Invalid RenderSize | Return null ImageData |
| backend failure | Internal Qt error | Log, return null |

## Examples

```cpp
// Scale to fit viewport
RenderSize target{800, 600};
ImageData scaled = RenderEngine::instance().scale(frame.pixels(), target);

// Manual command batch
std::vector<RenderCommand> batch;
batch.push_back(RenderCommand::drawImage(img, {800,600}, RenderInterp::Bilinear));
batch.push_back(RenderCommand::drawSelection({50,50,200,200}, 0xFF0000));
batch.push_back(RenderCommand::drawHistogram(hist, 256, {10,10,200,100}));

// Overlay diff at 50% alpha
ImageData blended = RenderEngine::instance().overlayDifference(base, diff, 0.5);

// Zoom ROI
RenderRect roi = {100, 100, 400, 400};
ImageData zoomed = RenderEngine::instance().scaleRegion(frame.pixels(), roi, {800,800});
```

## Unit Tests

```cpp
TEST(RenderCommand, FactoryDrawImage) {
    ImageData dummy = makeTestData(10, 10);
    auto c = RenderCommand::drawImage(dummy, {100,100}, RenderInterp::Bilinear);
    EXPECT_EQ(c.type, RenderCommandType::DrawImage);
    EXPECT_TRUE(c.srcImage.isValid());
    EXPECT_EQ(c.interp, static_cast<int>(RenderInterp::Bilinear));
}

TEST(RenderCommand, FactoryDrawSelection) {
    auto c = RenderCommand::drawSelection({10,10,50,50}, 0xFF00FF);
    EXPECT_EQ(c.type, RenderCommandType::DrawSelection);
    EXPECT_EQ(c.rect.x, 10);
    EXPECT_EQ(c.rgba, 0xFF00FF);
}

TEST(RenderEngine, ScaleNullInput) {
    ImageData out = RenderEngine::instance().scale(ImageData(), {100,100});
    EXPECT_TRUE(out.isNull());
}

TEST(RenderEngine, Scale100to50) {
    ImageData data = makeTestData(100, 100);
    ImageData out = RenderEngine::instance().scale(data, {50,50});
    EXPECT_EQ(out.width, 50);
    EXPECT_EQ(out.height, 50);
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenario `Render::scale(1920x1080→1280x720)`.

## Future Extension

- D2DRenderer (Direct2D Windows-only, GPU-accelerated scale)
- OpenGLRenderer (cross-platform)
- VulkanRenderer (high-perf compute)
- MetalRenderer (macOS)
- GPU-accelerated diff/heatmap via compute shaders (OpenCL/CUDA)
