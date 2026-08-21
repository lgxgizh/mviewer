# CompareEngine Specification

## Module

CompareEngine + controllers (SyncController · BlinkController · DifferenceEngine · SelectionController · ViewportController) + CompareSession

## Purpose

CompareEngine is the facade that owns comparison state (CompareSession) and routes operations to dedicated controllers. UI (CompareWorkspace) never modifies session directly — it reads `session()` after each mutation and renders.

## API

## Async loading (M28 P1-01)

CompareWorkspace::setImages() performs image loading **asynchronously**:

- Every requested path is submitted to the DecodePool via
  `ImageRepository::loadAsync` with `generateHistogram=false` (histograms are
  computed lazily by the UI).
- `setImages()` returns immediately; no frame is applied synchronously.
- When every request in the batch completes, the frames are delivered to the
  UI thread (QPointer + generation guard) and `CompareEngine::setFrames()` is
  invoked — the engine is mutated only on the UI thread, preserving the
  single-threaded ownership contract.
- A newer `setImages()` supersedes an in-flight batch (generation counter), so
  stale completions can never overwrite the current compare set (A -> B -> A is
  safe).
- `applySession()` called while a load is in flight is deferred and replayed by
  `finishLoad()` once the frames land (openCompare -> setImages -> applySession).

## Source-backed display regions (M48 P4)

When a Compare pane has only a metadata placeholder for a large source, the
workspace fits the pane from the probed source geometry before the first raster
arrives. The display worker then keeps that complete oriented source geometry while materializing
only the visible source rectangle (with a small overscan) once the pane is
deeply zoomed. The request maps displayed coordinates to raw decoder
coordinates through the EXIF contract, and the returned `coveredRect` is mapped
back before delivery. `RawImageView` draws that raster in its covered source
rectangle, so zoom/pan, selection, and crosshair coordinates remain in the full
source space. Latest-wins cancellation and generation guards apply equally to
full-frame LOD and region requests; geometric crop/rotation edits stay on the
full-frame preview path until their coverage transform is available.

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
    bool zoomEnabled = true;
    bool dragEnabled = true;
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
    // Adopt already-decoded frames (produced off the UI thread by the
    // CompareWorkspace async load path). Invalid/null frames are dropped,
    // matching setImages() semantics. Engine state is NOT thread-safe: call
    // this on the thread that owns the engine (the UI thread).
    void setFrames(std::vector<std::shared_ptr<ImageFrame>> frames);
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
| ----------- | ------ | ------------- | --------- |
| `paths` | `vector<string>` | Valid UTF-8 paths, non-empty | — |
| `index` | `int` | `[0, imageCount)` | — |
| `path` | `string` | Valid UTF-8 path | — |
| `on` | `bool` | Enable/disable sync | — |
| `scale` | `double` | >0 | `1.0` |
| `factor` | `double` | >0 | — |

## Output

| Method | Return | Semantics |
| -------- | -------- | ----------- |
| `setImages/addImage/removeImage/clear` | `void` | Rebuilds layout, resets transforms |
| `session()` | `CompareSession` | Immutable snapshot for UI |
| `differenceMap` | `ImageData` | Grayscale diff (RGB24); empty on size mismatch |
| `image(int)` | `const ImageFrame&` | Direct access; UB if index invalid |
| `imageAt(int)` | `const ImageFrame*` | Null if index invalid |

## Pixel Controller sampling contract

`PixelController::probe()` and `inspect()` sample every frame at the shared
image-space point through the single `samplePixel()` helper in
`core/image/ImageBuffer.h`, which canonicalises the stored pixel to RGBA
regardless of format (`p[i]` = i-th byte of the pixel in memory):

| PixelFormat   | r         | g         | b         | a         |
| ------------- | --------- | --------- | --------- | --------- |
| `RGB24`       | `p[0]`    | `p[1]`    | `p[2]`    | `255`     |
| `RGBA32`      | `p[0]`    | `p[1]`    | `p[2]`    | `p[3]`    |
| `BGR24`       | `p[2]`    | `p[1]`    | `p[0]`    | `255`     |
| `BGRA32`      | `p[2]`    | `p[1]`    | `p[0]`    | `p[3]`    |
| `Grayscale8`  | `p[0]`    | `p[0]`    | `p[0]`    | `255`     |

- **Grayscale replication**: a grayscale pixel stores one byte; the sample
  replicates it across r = g = b.
- **Alpha**: non-alpha formats always canonicalise to `a=255`; alpha formats
  surface the stored alpha byte.
- **Invalid result**: `PixelRGBA::valid` is `false` (RGB/A zeroed) when the
  image is null, the backing buffer is null or truncated so the pixel does not
  fully fit, or the coordinate is out of bounds. The sampler never dereferences
  memory in those cases — no out-of-bounds read.
- **One sample per frame**: `probe()` returns exactly one `PixelSample` per
  frame, in frame order; `inspect()` pairs them with per-cell
  `PixelDelta` against the base cell (index 0 by default). Delta semantics are
  unchanged: `dr/dg/db` = sample − base and `dist` = Euclidean distance in RGB
  space, defined only when both the base and the cell sample are valid.

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

## M35 display and transform semantics

- Metrics calculation and difference visualization are independent product
  states. Metrics may run in the background at all times; ordinary Compare has
  no overlay. Heatmap/highlight is rendered only after explicit opt-in.
- Fit is per pane: `fitScale[i] = min(viewportW/imageW, viewportH/imageH)`.
  Synchronized zoom applies one relative `zoomRatio` so
  `effectiveScale[i] = fitScale[i] * zoomRatio`.
- Uniform Pixel Scale is a separate opt-in mode and is the only state that
  requires equal absolute effective scales across panes.
- Rebuilt panes receive one coalesced next-event-loop Fit after layout settles.
  The MainWindow Compare host opens fullscreen before the single async load.

## M36 host and sync lifecycle semantics

`MainWindow` owns one Compare host (Option A). Opening Compare while another
host exists closes/replaces the old host. Each queued load captures its own
dialog and workspace; destruction of an old host cannot clear or redirect the
new host, and a MainWindow destruction safely drops queued work.

Sync persistence is four-state: `Off`, `Zoom`, `Drag`, and `All`. The two UI
axes are independent. Toggling either axis changes only future transform
propagation; it does not call `fitAll()` or reset the current viewport. Fit is
reserved for explicit Fit, first-load layout adaptation, or a required layout
rebuild.

## Performance

| Scenario | Budget | Baseline |
| ---------- | -------- | ---------- |
| `differenceMap(1080p)` | <25 ms | ~22 ms |
| `setImages(9)` | <10 ms | layout-only math |
| `fitCell` | <1 ms | pure math |
| `zoomAt` | <1 ms | per-cell transform |

## Errors

| Error | Cause | Recovery |
| ------- | ------- | ---------- |
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
