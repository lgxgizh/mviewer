# ImageRepository Lifecycle Specification

## Executive Summary

ImageRepository is the **sole entry point** for the complete image lifecycle:
**Discovery → Decode → State → Analysis → Thumbnail → Cache → Release**.

Other modules MUST NOT:

- Directly instantiate `ImageFrame`
- Access `DiskCache` or `ImageCache` directly for image data
- Retain raw `ImageData` outside of `shared_ptr<ImageFrame>`

All image state flows through ImageRepository. All engines (Decode, Render, Analysis)
accept `shared_ptr<ImageFrame>` and produce computed results (derived data), which are
stored back on the same `ImageFrame` (analysis cache, render cache, histogram).

## Lifecycle Diagram

```
┌────────────────────────────────────────────────────────────────────────┐
│                      Image Lifecycle                                   │
│                                                                        │
│  FileSystem            Repository            Frame                      │
│  ──────────           ──────────           ──────                      │
│  listImages()  ──►  loadAsync()   ──►  ImageFrame::create()           │
│                          │                  │                          │
│                          ▼                  ▼                          │
│                    DiskCache.get()    DecodeState::Decoded             │
│                          │                  │                          │
│                          ▼                  ▼                          │
│                    MemoryCache.put   AnalysisEngine::compute           │
│                          │                  │                          │
│                          ▼                  ▼                          │
│                    prefetch()        AnalysisCache populated           │
│                          │                  │                          │
│                          ▼                  ▼                          │
│                    release()         clearAnalysisCache()              │
│                    ───────                                             │
│                    invalidate()                                        │
│                    DiskCache.erase                                     │
│                    MemoryCache.erase                                   │
│                    analysis.frame = nullptr                            │
│                                                                        │
│  External code: shared_ptr<ImageFrame> (Repository-managed)            │
└────────────────────────────────────────────────────────────────────────┘
```

## Lifecycle Stages

### Stage 1: Discovery

**Owner:** `ImageRepository` (via `FileSystem::listImages`)

```
FileSystem::listImages(dirPath, maxImages)
    ↓
vector<string> filePaths   (sorted by SortMode)
    ↓
Repository caches metadata (path, mtime, size) in memory
```

**Post-conditions:**

- `repository.metadata(filePath)` returns valid `ImageMetadata` without decoding
- Image count = `filePaths.size()`

### Stage 2: Acquire / Load

**Entry point:** `ImageRepository::load` or `loadAsync`

```
load(filePath, opts)
    ├── if DiskCache.get(key, img):
    │       frame = ImageFrame::create(key, img)
    │       frame.setDecodeState(DecodeState::Decoded)
    │       frame.setCacheState(CacheState::Disk)
    │       Result{frame, fromCache=true}
    │
    ├── else:
    │       img = Decoder::decodeFull(filePath)
    │       DiskCache.put(key, img)
    │       MemoryCache.put(FullImage, key, img)
    │       frame = ImageFrame::create(key, img)
    │       Result{frame, fromCache=false}
    │
    └── on error{Result{success=false, error=...}}
```

**Post-conditions:**

- Returned `shared_ptr<ImageFrame>` has valid `pixels()`
- `DiskCache` contains the blob (if `opts.useDiskCache`)
- `CacheState` reflects hit/miss path

### Stage 3: State Mutation

**Ownership:** `ImageFrame` owns all per-frame mutable state.

| Field | Transition | Authorized Callers |
| ------- | ------------ | ------------------- |
| `DecodeState` | Idle → Decoding → Decoded/Failed | `ImageRepository` only |
| `CacheState` | None → Memory → Disk | `ImageRepository` only |
| `Histogram` | lazy-computed | `ImageRepository` + analyzer request |
| `Selection` | mutable | UI (via `setSelection`) |
| `Tags` | append/remove | UI (via `addTag`/`removeTag`) |
| `AnalysisCache` | per-analyzer populate | `AnalysisEngine` chain |
| `RenderCache` | per-tag populate | `RenderEngine` chain |

**Critical invariant:** Only ImageRepository mutates DecodeState and CacheState.
UI and analyzers MUST NOT directly modify these fields.

### Stage 4: Cache Promotion / Demotion

```
getUserAction("switch to next image")
    ↓
repository.prefetch(nextPath) ──► Decode Pool ──► DiskCache.put ──► MemoryCache.put
    ↓
MemoryCache.evict(LRU) when usage > viewerCacheSize (512 MB)
    ↓
DiskCache.put(lazy) via cacheToDisk() when app exits or on schedule
```

### Stage 5: Release

**Entry point:** `ImageRepository::release(filePath)`

Order of operations (MUST be atomic w.r.t. cache lookups):

1. Cancel any in-flight `loadAsync` task for this key (via TaskScheduler)
2. `MemoryCache.erase(key)` — tells `ImageCache` to drop all level entries for key
3. `DiskCache.remove(key)` — deletes SQLite blob
4. `MetadataStore.erase(key)` — drops `ImageMetadata`
5. All `shared_ptr<ImageFrame>` referencing this path become the sole pixel owners
   after cache release (reference counting handles final deallocation)

**Post-conditions:**

- `MemoryCache.get(key)` returns false
- `DiskCache.get(key)` returns false
- Any prior `shared_ptr<ImageFrame>` remains valid (heap-detached)

## Thread Safety

| Operation | Mechanism | Use Case |
| ----------- | ----------- | ---------- |
| `load` | `CacheManager` per-pool mutex | Any thread |
| `loadAsync` | TaskScheduler (raw QRunnable → DecodePool) | Background |
| `loadDirectory` | N × `loadAsync` tasks | Background thread spawn |
| `prefetch` | TaskScheduler BackgroundPool | Background only |
| `release` | Cancel + CacheManager atomic erase | Any thread |
| `metadata` | Atomic metadata store lookup | Any thread |

## Memory Ownership Model

```
┌────────────────────────────────────────────────────────────┐
│                   Ownership Graph                          │
│                                                            │
│  ImageRepository  (singleton)                              │
│       │ owns                                               │
│       ▼                                                    │
│  CacheManager  ──owns──► ImageCache[N]  ──owns──► ImageData│
│       │                      │                            │
│       │                      ▼                            │
│       │               DiskCache ──owns──► SQLite blobs    │
│       │                                                  │
│       ▼                                                  │
│  TaskScheduler  ──owns──► QThreadPool[5]                 │
│                                                            │
│  Users:                                                    │
│    UI threads: shared_ptr<ImageFrame> (borrowed ref)       │
│    Analyzer: shared_ptr<ImageFrame> + compute cached result│
│    Renderer: shared_ptr<ImageFrame> + RenderCache          │
│                                                            │
│  Rule: shared_ptr<ImageFrame> is the ONLY way to access   │
│  image state. No raw ImageData pointers escape the repo.   │
└────────────────────────────────────────────────────────────┘
```

## API Compliance Table

| Requirement | Current API | RFC-005 Compliance |
| ------------- | ------------- | ------------------- |
| Single entry for load | `load`, `loadAsync` | ✅ |
| Release path | `release` | ✅ |
| Cancel in-flight | Implicit via TaskScheduler cancel | ✅ |
| Analysis cache stored on frame | `ImageFrame::AnalysisCacheEntry` | ✅ |
| Histogram lazy | `ImageFrame::computeHistogram` | ✅ |
| Thumbnail lazy | `ImageFrame::setThumbnail` | ✅ |
| Metadata without decode | `metadata(filePath)` | ✅ |

## Error Handling

| Symptom | Detection | Recovery |
| --------- | ----------- | ---------- |
| Load after release | Cache miss | Re-load from File → Decode |
| Frame return after release | weak_ptr expired (if using weak) | Caller re-acquires via `load` |
| Disk full during cache write | SQLite `FULL` error | Log warning; memory cache only |
| Decode during cancel | `ctx.isCancelled()` returns true | Early exit; no frame returned |

## Future Extensions

- Deterministic ImageId (replace string path keys with 64-bit content hash)
- Reference counting per cache entry (promote on access)
- Load-order-aware prefetch priority
- Plugin-provided Decoder (ImageRepository delegates to registered decoders)
