# ImageFrame Specification

## Module

ImageFrame + RenderCacheEntry + AnalysisCacheEntry + DecodeState + CacheState

## Purpose

ImageFrame is the universal domain object: it holds pixels, metadata, thumbnail, histogram, decode/cache state, selection, tags, analysis cache, and render cache. All engines operate on ImageFrame; no external ad-hoc data structures. Thread-safe for read-only access; cache/decode state changes are atomic.

## API

```cpp
enum class DecodeState : uint8_t { Idle, Decoding, Decoded, Failed };
enum class CacheState  : uint8_t { None, Memory, Disk };

struct AnalysisCacheEntry {
    std::string analyzerName;
    bool ok = false;
    bool populated = false;
};

struct RenderCacheEntry {
    enum class Tag : uint8_t { ScaledView, ThumbnailOverlay, DiffOverlay };
    Tag tag;
    ImageData data;
    int srcWidth = 0;
    int srcHeight = 0;
};

class ImageFrame {
public:
    ImageFrame() = default;
    ImageFrame(const mviewer::domain::ImageMetadata& meta, const ImageData& pixels);

    // Factory: create from decoded image, filling metadata (hash, size, mtime).
    // Implementation may use Qt for file interrogation (.cpp only).
    static ImageFrame create(const std::string& path, const ImageData& pixels);

    // Identity
    const mviewer::domain::ImageMetadata& metadata() const;
    mviewer::domain::ImageId id() const;

    // Pixel access
    const ImageData& pixels() const;
    const ImageBuffer view() const;
    int width() const;
    int height() const;
    bool isValid() const;

    // Thumbnail (lazy)
    const ImageData& thumbnail() const;
    void setThumbnail(const ImageData& t);
    bool hasThumbnail() const;

    // Decode / Cache states
    DecodeState decodeState() const;
    void setDecodeState(DecodeState s);
    CacheState cacheState() const;
    void setCacheState(CacheState s);

    // Histogram (lazy-computed)
    const mviewer::domain::Histogram& histogram() const;
    void computeHistogram();
    bool hasHistogram() const;

    // Metadata mutation
    void setMetadata(const mviewer::domain::ImageMetadata& m);

    // Selection
    const mviewer::domain::Selection& selection() const;
    void setSelection(const mviewer::domain::Selection& s);
    void clearSelection();

    // Tags
    const std::vector<std::string>& tags() const;
    void addTag(const std::string& tag);
    void removeTag(const std::string& tag);
    bool hasTag(const std::string& tag) const;

    // Analysis cache
    const AnalysisCacheEntry* findAnalysis(const std::string& analyzer) const;
    const std::vector<AnalysisCacheEntry>& analysisCache() const;
    void setAnalysisResult(const std::string& analyzer, bool ok);
    void clearAnalysisCache();

    // Render cache
    const RenderCacheEntry* findRenderCache(RenderCacheEntry::Tag tag) const;
    void setRenderCache(const RenderCacheEntry& entry);
    void clearRenderCache();

    // Stats convenience
    double luminanceMean() const;
    void rgbMeans(double& r, double& g, double& b) const;
};
```

## Input

| Parameter | Type | Constraints | Default |
| ----------- | ------ | ------------- | --------- |
| `meta` | `const ImageMetadata&` | Valid (width/height >0) | — |
| `pixels` | `const ImageData&` | Matches meta dimensions | — |
| `t` | `const ImageData&` | Optional smaller-than-full thumbnail | — |
| `tag` | `string` | Non-empty, unique per tag | — |
| `analyzer` | `string` | Analyzer ID (e.g., "histogram") | — |

## Output

| Method | Return | Semantics |
| -------- | -------- | ----------- |
| `create(path, pixels)` | `ImageFrame` | Populates metadata from path |
| `pixels()` | `const ImageData&` | Full-resolution pixels; null if not decoded |
| `thumbnail()` | `const ImageData&` | Lazy thumbnail; call `hasThumbnail()` first |
| `histogram()` | `const Histogram&` | Auto-computes on first call if needed |
| `findAnalysis(name)` | `const AnalysisCacheEntry*` | Null if not cached |
| `findRenderCache(tag)` | `const RenderCacheEntry*` | Null if not cached |

## Ownership

- ImageFrame **owns** its pixel data, thumbnail, histogram, selection, tags, analysis cache, and render cache.
- All cached data is heap-allocated and released when ImageFrame is destroyed.
- `shared_ptr<ImageFrame>` is passed to engines; factories (ImageRepository) create the object once.

## Thread Safety

| Method | Thread | Mechanism |
| -------- | -------- | ----------- |
| `pixels()`, `view()`, `width()`, `height()`, `isValid()` | Any thread | Read-only, safe concurrent |
| `decodeState()`, `cacheState()` | Any thread | Atomic enum access |
| `setDecodeState()`, `setCacheState()` | Any thread | Atomic store |
| `computeHistogram()` | Any thread | Idempotent; re-entrant safe (may double-compute) |
| `addTag()/removeTag()` | UI thread | Not thread-safe (modifies vector) |
| `setAnalysisResult()` | UI/worker | Lock-free if used sequentially |

## Memory

| Component | Dominant Allocation |
| ----------- | --------------------- |
| `pixels()` | `w*h*3` bytes (RGB24) |
| `thumbnail()` | `64*64*3` bytes typical (256×256 max) |
| `histogram()` | 4 × 256 ints = 4 KB |
| `analysisCache()` | 128 bytes × analyzer count |
| `renderCache()` | 8 + image bytes per entry |

## Performance

| Scenario | Budget | Baseline |
| ---------- | -------- | ---------- |
| `create()` (with metadata) | <0.5 ms | hash + QFileInfo |
| `computeHistogram(1080p)` | <5 ms | ~3 ms |
| `findAnalysis()` | <0.01 ms | vector lookup |
| `findRenderCache()` | <0.01 ms | vector lookup |

## Errors

| Error | Cause | Recovery |
| ------- | ------- | ---------- |
| `invalid pixels` | Decode failed | `decodeState()` returns `Failed`; `isValid()` returns false |
| `null selection` | Cleared | `selection()` returns default empty Selection |
| `tag duplicate` | Already has tag | `addTag` is idempotent (no-op) |
| `analyzer not found` | Not registered | `findAnalysis` returns null |

## Examples

```cpp
// Create from decoded data
ImageData pixels = decode("photo.jpg");
auto frame = ImageFrame::create("photo.jpg", pixels);

// Lazy histogram (auto-computed on first access)
frame.computeHistogram();
std::cout << "luminance mean: " << frame.luminanceMean() << "\n";

// Tags
frame.addTag("favorite");
frame.addTag("red-team");
if (frame.hasTag("favorite")) markAsFavorite();

// Analysis caching
frame.setAnalysisResult("psnr", true);
if (auto* e = frame.findAnalysis("psnr")) {
    std::cout << "PSNR computed: " << e->ok << "\n";
}

// Render cache
RenderCacheEntry entry;
entry.tag = RenderCacheEntry::Tag::ScaledView;
entry.data = scaled;
entry.srcWidth = frame.width();
entry.srcHeight = frame.height();
frame.setRenderCache(entry);
```

## Unit Tests

```cpp
TEST(ImageFrame, IsValidWhenPixelsSet) {
    ImageData pixels = makeTestData(100, 100);
    ImageFrame frame(makeMeta(), pixels);
    EXPECT_TRUE(frame.isValid());
    EXPECT_EQ(frame.width(), 100);
    EXPECT_EQ(frame.height(), 100);
}

TEST(ImageFrame, IdFromMetadata) {
    ImageFrame frame;
    frame.setMetadata(makeMeta(256, 256));
    EXPECT_EQ(frame.id(), makeMeta(256, 256).id);
}

TEST(ImageFrame, SelectionRoundTrip) {
    ImageFrame frame;
    mviewer::domain::Selection sel = {10, 10, 50, 50};
    frame.setSelection(sel);
    EXPECT_EQ(frame.selection().x, 10);
    frame.clearSelection();
    EXPECT_EQ(frame.selection().x, 0);
}

TEST(ImageFrame, TagsAreUnique) {
    ImageFrame frame;
    frame.addTag("a");
    frame.addTag("a");
    EXPECT_EQ(frame.tags().size(), 1);
    frame.removeTag("a");
    EXPECT_FALSE(frame.hasTag("a"));
}

TEST(ImageFrame, AnalysisCacheRoundTrip) {
    ImageFrame frame;
    frame.setAnalysisResult("psnr", true);
    auto* e = frame.findAnalysis("psnr");
    EXPECT_NE(e, nullptr);
    EXPECT_TRUE(e->populated);
    EXPECT_TRUE(e->ok);
}

TEST(ImageFrame, RenderCacheLookup) {
    ImageFrame frame;
    RenderCacheEntry entry;
    entry.tag = RenderCacheEntry::Tag::ScaledView;
    entry.data = makeTestData(64, 64);
    frame.setRenderCache(entry);
    EXPECT_NE(frame.findRenderCache(RenderCacheEntry::Tag::ScaledView), nullptr);
}
```

## Benchmark

See `benchmarks/benchmark_main.csv` scenario `Frame::create(1920x1080)`.

## Future Extension

- Versioned analysis cache (track data version per analyzer)
- Render cache with LRU eviction (bounded memory budget)
- Thumbnail pyramid (multiple resolutions for zoom levels)
- Anomaly flag for "very different from most" images (ML-assisted)
