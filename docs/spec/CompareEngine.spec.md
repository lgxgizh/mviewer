# CompareEngine Specification

## Overview

CompareEngine manages multi-image comparison. It owns CompareSession state and routes operations to dedicated controllers.

## API

```cpp
class CompareEngine {
public:
    // Image management
    void setImages(const std::vector<std::string>& paths);
    void addImage(const std::string& path);
    void removeImage(int index);
    void clear();

    // Session state (read-only view)
    mviewer::domain::CompareSession session() const;

    // Transform
    void setSyncEnabled(bool on);
    void setScale(double s);
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);
    void fitCell(int index, const CellSize& viewport, const CellSize& imageSize);

    // Difference
    ImageData differenceMap(int index, int baseIndex = 0);

    // Blink
    void setBlinkIndex(int idx);
    int blinkIndex() const;
};
```

## Subsystems

### SyncController
- Input: target scale, offset, viewport
- Output: per-cell transforms
- Sync: broadcast scale to all cells (if enabled)

### DifferenceEngine
- Input: two ImageData (same size)
- Output: grayscale diff map + heatmap

### BlinkController
- Input: interval_ms, active flag
- Output: current highlighted index

## Ownership

- CompareEngine owns CompareSession (mutable)
- Workspace reads session, never modifies

## Errors

| Error | Condition |
|-------|-----------|
| Size mismatch | Diff dimensions differ < min(a,b) |
| Invalid index | Index >= imageCount |

## Tests

```cpp
TEST(Compare, SetImagesRebuildsLayout) {
    CompareEngine e;
    e.setImages({"a.png", "b.png", "c.png"});
    EXPECT_EQ(3, e.imageCount());
}

TEST(Compare, DifferenceSameReturnsBlack) {
    auto diff = AnalysisEngine::differenceMap(sameA, sameB);
    // all pixels == 0
}
```
