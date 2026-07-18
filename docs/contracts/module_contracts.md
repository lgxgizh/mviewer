# Contracts

## Module: ImageFrame

**Owner:** ImageRepository  
**Purpose:** Universal domain object holding all image-related data: pixels, metadata, histogram, render cache, analysis cache, decode/cache state, selection, tags.

| Field | Type | Owner | Access |
| --- | --- | --- | --- |
| `pixels` | `const ImageFrame*` | ImageRepository | Read-only |
| `metadata` | `const ImageMetadata&` | ImageFrame.create() | Immutable |
| `thumbnail` | `ImageData` | ImageRepository/decoder | Populated lazily |
| `histogram` | `Histogram` | ImageFrame.computeHistogram() | Thread-safe (first-access compute, then const) |
| `decodeState` | `std::atomic<DecodeState>` | ImageRepository | Atomic store/load |
| `cacheState` | `std::atomic<CacheState>` | CacheManager | Atomic store/load |
| `selection` | `Selection` | Workspace/user | Mutable |
| `tags` | `vector<string>` | User/tags | Mutable |
| `analysisCache` | `vector<AnalysisCacheEntry>` | Analyzer plugins | Analyzer writes once, many reads |
| `renderCache` | `vector<RenderCacheEntry>` | RenderEngine | UI thread writes, render reads |

## Module: CacheManager

**Owner:** CacheManager singleton  
**5-level hierarchy:**

| Level | Purpose | Capacity | Eviction |
| --- | --- | --- | --- |
| Metadata | ImageMetadata objects | 16 MB | LRU (entry count) |
| Thumbnail | List/gallery thumbnails | 64 MB | LRU (byte pool) |
| Preview | Preview panel semi-large | 256 MB | LRU (byte pool) |
| Viewer | Full-resolution decode | 512 MB | LRU (byte pool) |
| Disk | Persistent SQL cache | 1 GB / 100k entries | Lazy eviction |

- `get(level, key, out)` = memory-first → disk-fillback semantics
- `put(level, key, img)` = write-through to target level
- `invalidate(key)` + `erase(key)` = full purge across all layers
- `putMetadata/getMetadata/hasMetadata` = object store independent of byte pools

## Module: TaskScheduler

**Queues (Priority-highest to lowest):** UI, Decode, Thumbnail, Analysis, Background

**Task structure:**

| Field | Type | Notes |
| --- | --- | --- |
| `id` | `uint64_t` (auto-inc) | Unique per task |
| `cancel` | `shared_ptr<atomic<bool>>` | Task-local cancel; TaskHandle crosses threads |
| `progress` | `shared_ptr<atomic<int>>` | 0-100 |
| `onProgress` | `function<void(int)>` | UI callback |
| `dependencies` | `vector<TaskId>` | Poll-based waiter inside LambdaTask |

**Cancel propagation:** `cancel(task)` cancels own token. `cancelTree(id)` BFS-walks dep graph and cancels all descendants.

---

## Module: Analyzer Registry

Interface: `Analyzer { name(), description(), analyze(frame), analyzeRegion(frame, region); }`

Register via global static: `registerAnalyzer(id, factory)`.

Built-in IDs: `histogram`, `rgbmean`, `noise`, `psnr`, `ssim`, `entropy`, `sharpness`.

**Future:** `AnalyzerCapability` (single/multi image/ROI/streaming/GPU) — agents query automatically.

## Module: RenderEngine

| `Renderer` | Backend interface | `scale`, `overlayDifference`, `scaleRegion` |
| --- | --- | --- |
| `SoftwareRenderer` | Qt-backed (impl detail) | Current default |
| `RenderEngine` | Facade | `setBackend()`, instance + static compat API |
| `RenderCommand` | Flat struct for composable draw ops | `DrawImage`, `DrawOverlay`, `DrawHistogram`, `DrawSelection`, `DrawPixelMarker` |

**Future backends:** D2D / OpenGL / Vulkan / Metal.

## Module: CompareEngine

State is **solely** owned by `mviewer::domain::CompareSession`. Facade exposes controllers as thin wrappers.

| Controller | Responsibility |
| --- | --- |
| `SyncController` | Shared zoom/pan/scroll |
| `BlinkController` | Alternating highlight |
| `DifferenceEngine` | Pixel diff + heatmap |
| `SelectionController` | Per-cell transform + ROI box |
| `ViewportController` | Layout (cols / rows / cell pos / cell size) |
