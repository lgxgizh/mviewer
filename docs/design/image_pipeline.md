# Image Pipeline

This document defines the full image data flow and serves as a reference for
all feature implementation and bug identification.

```
┌─────────────────────────────────────────────────────────────────────┐
│                        Image Pipeline                               │
│                                                                     │
│  Directory ──► Repository ──► Decoder ──► ImageFrame                │
│      │             │                            │                   │
│      │             ▼                            ▼                   │
│      │      ┌────────────┐              ┌────────────┐             │
│      │      │  Analyzer   │              │   Cache     │             │
│      │      │  Pipeline   │              │  Hierarchy  │             │
│      │      └─────┬──────┘              └─────┬──────┘             │
│      │            │                           │                    │
│      │            ▼                           ▼                    │
│      │       ┌─────────┐              ┌──────────────┐            │
│      │       │ Analysis │              │ RenderEngine │            │
│      │       │  Cache   │              └──────┬───────┘            │
│      │       └─────────┘                     │                     │
│      │            │                          │                     │
│      │            ▼                          ▼                     │
│      │       ┌──────────┐            ┌──────────────┐             │
│      │       │  Stats   │            │   Display    │             │
│      │       │  HUD     │            │   Surface    │             │
│      │       └──────────┘            └──────────────┘             │
│      │                                                              │
│      ▼                                                              │
│  ┌─────────┐     ┌──────────┐     ┌──────────┐                  │
│  │ Workspace│────►│ Compare  │────►│  UI      │                  │
│  │ Manager  │     │ Session  │     │ Render   │                  │
│  └─────────┘     └──────────┘     └──────────┘                  │
└─────────────────────────────────────────────────────────────────────┘
```

## Pipeline Stages

### Stage 1: Discovery (Directory → Repository)

| Input | Output | Owner | Thread | Buycle |
|-------|--------|-------|--------|--------|
| `dirPath` (string) | `vector<string>` image files | `ImageRepository` | Background Pool | <50ms / 1000 files |

**Bug Symptoms:** No images appear, or images appear in wrong order.
**Check:** `FileSystem::listImages()`, file filter regex, sort comparator.

### Stage 2: Decode (Repository → Decoder)

| Input | Output | Owner | Thread | Budget |
|-------|--------|-------|--------|--------|
| `filePath`, `LoadOptions` | `shared_ptr<ImageFrame>` | `ImageRepository::load` | Decode Pool | <50ms cold, <1ms warm |

**Bug Symptoms:** Image is black, partially decoded, or takes too long.
**Check:** `Decoder::decodeFull()`, `DiskCache` hit/miss, `DecodeState` transitions.

### Stage 3: State (Frame → Cache)

| Input | Output | Owner | Thread | Budget |
|-------|--------|-------|--------|--------|
| `ImageFrame` | cached pixel/thumbnail/histogram data | `CacheManager` | Decode Pool | <10ms |

**Bug Symptoms:** Switching images is slow, or memory grows unbounded.
**Check:** `CacheManager` eviction policy, `DiskCache` byte budget, LRU counters.

### Stage 4: Analyze (Frame → Analyzer)

| Input | Output | Owner | Thread | Budget |
|-------|--------|-------|--------|--------|
| `ImageFrame`, request type | `ImageStats`/`ImageData` result | `AnalysisEngine` | Analysis Pool | <100ms |

**Bug Symptoms:** UI freezes during analysis, or histogram never updates.
**Check:** `TaskScheduler` queue depth, `AnalysisCacheEntry` populate state.

### Stage 5: Render (Frame → Display)

| Input | Output | Owner | Thread | Budget |
|-------|--------|-------|--------|--------|
| `ImageFrame`, viewport, transform | `QPixmap` ready for UI | `RenderEngine` | UI thread | <16ms |

**Laggy zoom/pan, flicker on image switch.
**Check:** `RenderEngine::render()`, QPainter state, widget update rect.

---

## Boundary Contracts

Each stage has well-defined inputs and outputs. Bugs manifest at stage boundaries.
To locate a bug, identify which stage produces wrong output — then trace inputs.

| Boundary | Symptoms | Likely Cause |
|----------|----------|--------------|
| Directory → Repository | Missing files, wrong order | `FileSystem` filter, mtime sort |
| Repository → Decoder | Black/corrupt image, slow | `Decoder` codec, `DiskCache` mismatch |
| Decoder → Frame | Library hangs, no histogram | `ImageFrame::create()`, pool exhaustion |
| Frame → Cache | Slow switching, OOM | `CacheManager` eviction, `DiskCache` bytes |
| Cache → Analyzer | Stale stats | `AnalysisCacheEntry::populated` race |
| Analyzer → UI | No histogram update | `onProgress` not dispatched to UI thread |

## How to Use This Document

**When implementing a feature:** place it in the corresponding stage. The stage
owns its data; other stages must NOT maintain parallel state.

**When fixing a bug:** find the stage where output becomes wrong, then inspect
inputs and the stage's processing.

**When adding a metric/benchmark:** benchmark each stage independently. CI will
catch regressions in stage-level budgets.
