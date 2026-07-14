# CompareEngine Specification

## Module
CompareEngine + controllers (SyncController · BlinkController · DifferenceEngine · SelectionController · ViewportController) + CompareSession

## Purpose
CompareEngine is the facade that owns comparison state (CompareSession) and routes operations to dedicated controllers. UI (CompareWorkspace) never modifies session directly — it reads `session()` after each mutation and renders.

## API

```cpp
// Core structures (declared in CompareEngine.h)
struct CellPoint { int x = 0; int y = 0; };
struct CellSize  { int w = 0; int h = 0; };
struct Vec2      { double x = 0.0; double y = 0.0; };

struct CompareLayout {
    int cols = 0, rows = 0, imageCount = 0;
    static CompareLayout forCount(int n);
    CellPoint cellPos(int index, const CellSize& viewport) const;
    CellSize cellSize(const CellSize& viewport) const;
};

struct SyncTransform {
    double scale = 1.0;
    Vec2 offset;
    bool enabled = true;
};

struct CellTransform {
    double scale = 1.0;
    Vec2 offset;
};

class CompareEngine {
public:
    CompareEngine();

    // Image management
    void setImages(const std::vector<std::string>& paths);
    void addImage(const std::string& path);
    void removeImage(int index);
    void clear();
    int imageCount() const;
    const ImageFrame& image(int index) const;
    const ImageFrame* imageAt(int index) const;

    // Layout (read-only)
    const CompareLayout& layout() const;

    // Session state (read-only view - UI consumes, never writes)
    mviewer::domain::CompareSession session() const;

    // Synchronized transform
    const SyncTransform& syncTransform() const;
    void setSyncEnabled(bool on);
    bool syncEnabled() const;
    void setScale(double s);
    void setOffset(double ox, double oy);
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);

    // Per-cell transform (when sync disabled)
    double cellScale(int index) const;
    Vec2 cellOffset(int index) const;
    void setCellScale(int index, double s);
    void setCellOffset(int index, double ox, double oy);
    const CellTransform& cellTransform(int index) const;

    // Fit cell to viewport (contain)
    void fitCell(int index, const CellSize& viewport, const CellSize& imageSize);

    // Blink (single-image highlight)
    int blinkIndex() const;
    void setBlinkIndex(int idx);
    void clearBlink();

    // Difference
    ImageData differenceMap(int index, int baseIndex = 0);
};

// Internal controllers (in CompareEngine.cpp)
namespace mvcore {
class SyncController { /* broadcast scale/offset to all cells */ };
class BlinkController { /* toggle highlight index */ class DifferenceEngine {
public:
    ImageData differenceMap(const ImageData& a, const ImageData& b);
    ImageData heatMap(const ImageData& gray);
};
class SelectionController { /* per-cell marquee selection */ };
class ViewportController { /* fit-to-cell math */ };
}
```

## Input

| Parameter | Type | Constraints | Default |
|-----------|------|-------------|---------|
| `paths` | `vector<string>` | Valid UTF-8 paths, non-empty | — |
| `index` | `int` | `[0, imageCount)` | — |
| `path` | `string` | Valid UTF-8 path | — |
| `on` | `bool` | Enable/disable sync | — |
| `scale` | `double` | >0 | `1.0` |
| `factor` | `double` | >0 | — |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `setImages/addImage/removeImage/clear` | `void` | Rebuilds layout, resets transforms |
| `session()` | `CompareSession` | Immutable snapshot for UI |
| `differenceMap` | `ImageData` | Grayscale diff (RGB24); empty on size mismatch |
| `image(int)` | `const ImageFrame&` | Direct access; UB if index invalid |
| `imageAt(int)` | `const ImageFrame*` | Null if index invalid |

## Ownership

- CompareEngine **owns** CompareSession (mutable container).
- CompareEngine **owns** all controllers (by value member in .cpp).
- CompareWorkspace reads `session()` but never writes.
- ImageFrame instances come from ImageRepository/cache (CompareEngine stores them by value in `vector<ImageFrame>`).

## Thread Safety

| Thread | Use |
|--------|-----|
| UI thread | All public mutations |
| Background | None (compare is synchronous) |

## Memory

| Operation | Dominant Allocation |
|-----------|---------------------|
| `differenceMap` | `w*h*3` bytes (output ImageData) |
| `setImages(n)` | n × ImageFrame stored by value (frames are lightweight handles; pixel data is shared) |

## Performance

| Scenario | Budget | Baseline |
|----------|--------|----------|
| `differenceMap(1080p)` | <25 ms | ~22 ms |
| `setImages(9)` | <10 ms | layout-only math |
| `fitCell` | <1 ms | pure math |
| `zoomAt` | <1 ms | per-cell transform |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `size mismatch` | Diff dimensions differ | Clip to min(w,h); return empty if severe |
| `invalid index` | Index out of range | Return null / no-op |
| `null frame` | ImageFrame not loaded | Skip; return empty diff |

## Examples

```cpp
CompareEngine engine;
engine.setImages({"a.png", "b.png", "c.png"});

// Synchronized zoom
engine.setSyncEnabled(true);
engine.setScale(2.0);

// Difference view
ImageData diff = engine.differenceMap(1); // vs base=0

// Fit-to-cell
engine.fitCell(0, {800, 600}, {1920, 1080});

// Blink comparison
engine.setBlinkIndex(0);
```

## Unit Tests

```cpp
TEST(Compare, SetImagesRebuildsLayout) {
    CompareEngine e;
    e.setImages({"a.png", "b.png", "c.png"});
    EXPECT_EQ(e.imageCount(), 3);
}

TEST(Compare, RemoveShrinks) {
    CompareEngine e;
    e.setImages({"a.png", "b.png"});
    e.removeImage(0);
    EXPECT_EQ(e.imageCount(), 1);
}

TEST(Compare, SyncPropagatesScale) {
    CompareEngine e;
    e.setImages({"a.png", "b.png"});
    e.setSyncEnabled(true);
    e.setScale(2.0);
    for (int i = 0; i < e.imageCount(); ++i)
        EXPECT_NEAR(e.cellTransform(i).scale, 2.0, 1e-6);
}

TEST(Compare, BlinkToggles) {
    CompareEngine e;
    e.setBlinkIndex(3);
    EXPECT_EQ(e.blinkIndex(), 3);
    e.clearBlink();
    EXPECT_EQ(e.blinkIndex(), -1);
}

TEST(Compare, DifferenceNullOnMismatch) {
    ImageData a = makeTestData(100, 100);
    ImageData b = makeTestData(200, 200);
    auto diff = AnalysisEngine::differenceMap(a, b);
    EXPECT_TRUE(diff.isNull());
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenario `Compare::differenceMap(1920x1080)`.

## Future Extension

- Side-by-side layout presets (strip, grid, overlay, mirror)
- Per-cell independent color channels (R/G/B/A isolation)
- Animation of diff transitions (UI tile)
- Pixel-shape matching (ADR-driven, requires explicit request)
