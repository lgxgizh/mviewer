# CompareSession Specification

## Module
CompareSession + CellTransform + SyncMode + Viewport + CompareSelection

## Purpose
CompareSession is the sole owner of all comparison state (RFC-007). UI (CompareWorkspace) reads the session after each mutation and renders; it never owns state. CompareEngine builds and mutates the session; UI consumes it.

## API

```cpp
namespace mviewer::domain {

struct CellTransform {
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
};

enum class SyncMode : uint8_t { Off, Zoom, Drag, All };

struct Viewport {
    int width = 0;   // workspace width px
    int height = 0;  // workspace height px
    int cellW = 0;   // per-cell width px
    int cellH = 0;   // per-cell height px
    int cols = 0;
    int rows = 0;
};

struct CompareSelection {
    int x = 0, y = 0, w = 0, h = 0;
    bool active = false;
    bool synced = false;  // sync selection across cells
};

struct CompareSession {
    static constexpr int MAX_IMAGES = 8;

    std::vector<std::string> imageIds;
    std::vector<CellTransform> cells;
    SyncMode syncMode = SyncMode::All;
    int blinkIndex = -1;

    double sharedScale = 1.0;
    double sharedOffsetX = 0.0;
    double sharedOffsetY = 0.0;

    int cols = 0, rows = 0;

    Viewport viewport;
    CompareSelection selection;

    int imageCount() const;
    bool isValid() const;                          // imageCount >= 2 && <= MAX_IMAGES
    bool isComparing() const;                     // convenience alias for isValid()
    bool isBlinking() const;                      // blinkIndex in [0, imageCount)
    bool isSyncOn() const;                        // syncMode != Off
};

} // namespace mviewer::domain
```

## Input

CompareSession is not directly constructed by UI. CompareEngine builds it:
```cpp
CompareSession s = engine.session();  // snapshot for rendering
```

Fields are populated by CompareEngine::setImages() and transform mutations.

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `imageCount()` | `int` | Number of loaded images |
| `isValid()` | `bool` | true if 2..8 images loaded |
| `isComparing()` | `bool` | Alias for isValid() |
| `isBlinking()` | `bool` | true if blinkIndex is valid and in range |
| `isSyncOn()` | `bool` | true if syncMode != Off |

## Ownership

- CompareEngine **owns** the live CompareSession (mutable).
- UI receives a **copy** before each render; changes by engine are not visible until next `session()` call.
- No shared ownership; CellTransform is stored by value in vector.
- CompareSession is a plain struct; no heap allocations except `vector<string>` for imageIds.

## Thread Safety

| Thread | Use |
|--------|-----|
| UI thread | Reads session snapshot (copy) for rendering |
| Engine thread | Mutates live session; UI reads only via `session()` snapshot |
| Copy | Copy is independent; UI outlives the mutation |

## Memory

| Component | Size |
|-----------|------|
| `imageIds` | ~64 bytes × count |
| `cells` | 24 bytes × count (3 doubles per CellTransform) |
| `viewport` | 24 bytes |
| `selection` | 20 bytes |
| **Total** | ~200 bytes per session |

## Performance

| Operation | Budget | Baseline |
|-----------|--------|----------|
| `session()` (copy) | <0.01 ms | shallow string copies |
| `isComparing()` | <0.001 ms | arithmetic only |
| `isBlinking()` | <0.001 ms | bounds check |

## Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `imageCount() < 2` | Session not yet populated | `isValid()` returns false; UI shows "load 2+ images" |
| `blinkIndex out of range` | Race set during image removal | `isBlinking()` returns false; ignore |
| `invalid syncMode` | Bad cast | Default to SyncMode::Off |

## Examples

```cpp
// Read current session for rendering
mviewer::domain::CompareSession s = engine.session();

// Guard: only render comparison when valid
if (!s.isComparing()) return;

// Per-cell transform
for (int i = 0; i < s.imageCount(); ++i) {
    auto& cell = s.cells[i];
    render(cell.scale, cell.offsetX, cell.offsetY);
}

// Blink mode
if (s.isBlinking()) {
    highlightOnly(s.blinkIndex);
}

// Sync state
bool zoomSync = (s.syncMode == SyncMode::Zoom || s.syncMode == SyncMode::All);
bool dragSync = (s.syncMode == SyncMode::Drag || s.syncMode == SyncMode::All);
```

## Unit Tests

```cpp
TEST(CompareSession, EmptyNotComparing) {
    mviewer::domain::CompareSession s;
    EXPECT_FALSE(s.isValid());
    EXPECT_FALSE(s.isComparing());
    EXPECT_EQ(s.imageCount(), 0);
}

TEST(CompareSession, TwoImagesValid) {
    mviewer::domain::CompareSession s;
    s.imageIds = {"a.png", "b.png"};
    EXPECT_TRUE(s.isValid());
    EXPECT_TRUE(s.isComparing());
    EXPECT_EQ(s.imageCount(), 2);
}

TEST(CompareSession, TooManyImagesInvalid) {
    mviewer::domain::CompareSession s;
    for (int i = 0; i < 10; ++i) s.imageIds.push_back("x.png");
    EXPECT_FALSE(s.isValid()); // exceeds MAX_IMAGES=8
}

TEST(CompareSession, BlinkDetection) {
    mviewer::domain::CompareSession s;
    s.imageIds = {"a.png", "b.png", "c.png"};
    s.blinkIndex = -1;
    EXPECT_FALSE(s.isBlinking());
    s.blinkIndex = 1;
    EXPECT_TRUE(s.isBlinking());
    s.blinkIndex = 5; // out of range
    EXPECT_FALSE(s.isBlinking());
}

TEST(CompareSession, SyncModeDetection) {
    mviewer::domain::CompareSession s;
    s.syncMode = SyncMode::All;
    EXPECT_TRUE(s.isSyncOn());
    s.syncMode = SyncMode::Off;
    EXPECT_FALSE(s.isSyncOn());
    s.syncMode = SyncMode::Zoom;
    EXPECT_TRUE(s.isSyncOn());
}

TEST(CompareSession, ViewportMath) {
    mviewer::domain::CompareSession s;
    s.viewport = {1920, 1080, 960, 540, 2, 1};
    EXPECT_EQ(s.viewport.cols, 2);
    EXPECT_EQ(s.viewport.cellW, 960);
    EXPECT_EQ(s.viewport.cellH, 540);
}
```

## Benchmark

Not a hot-path concern; session copy costs ~0.01 ms for 8 images.

## Future Extension

- Per-cell SyncMode override (zoom-sync on, drag-sync off independently)
- Session history (undo/redo stack for transforms)
- Named-presets ("side-by-side", "triple", "quad")
- Per-session color-profile (sRGB/AdobeRGB/Display P3)
