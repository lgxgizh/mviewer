# Data Flow Overview

## End-to-End Lifecycle

```
File path
   ↓
ImageRepository::load()
   ↓
DiskCache hit? → ImageData pixels
   ↓ miss
Decoder::decodeFull() → ImageData pixels
   ↓
ImageFrame::create(path, pixels) → ImageFrame (metadata filled)
   ↓
CacheManager::put(Memory::Viewer, key, pixels)
   ↓
Engine::process(frame) → new ImageData / stats / analysis
   ↓
ImageFrame::setRenderCache(entry) / ImageFrame::setAnalysisResult(...)   ← owns derived data
   ↓
UI reads ImageFrame + RenderCache → scale + overlayDifference → screen
```

## Ownership Chain

```
ImageRepository (singleton)
   ↓ owns
ImageFrame (one per image)
   ↓ owns
AnalysisCacheEntry[] (one per analyzer)
RenderCacheEntry[] (one per Tag)
   ↓ reader
UI / Worker threads
```

## Cache Flow (read path)

```
UI thread calls RenderEngine::scale(image, target)
   ↓
RenderCache lookup (image.findRenderCache(Tag::ScaledView))
   ↓ miss
Backend (SoftwareRenderer) → scale via Qt
   ↓
RenderCache::entry stored in ImageFrame
   ↓
Subsequent reads: hit RenderCache → return cached
```

## Cache Flow (write path)

```
Worker thread calls ImageRepository::load(path)
   ↓
DiskCache::get → hit → return pixels
   ↓ miss
Decoder::decodeFull
   ↓
DiskCache::put (background)
   ↓
ImageCache::put(Memory::Viewer) — bounded by LRU eviction
   ↓
CacheManager::putMetadata(key, frame->metadata())
```

## External Consumers / Producers

![data_flow](data_flow.puml)

```
@startuml
rectangle "File System" as FS
rectangle "ImageRepository" as IR
rectangle "CacheManager" as CM
rectangle "TaskScheduler" as TS
rectangle "CompareEngine" as CE
rectangle "AnalysisEngine" as AE
rectangle "RenderEngine" as RE
rectangle "UI" as UI

FS --> IR : file bytes
IR --> CM : put / get
TS --> IR : "loadAsync"
TS --> AE : analyze
IR --> CE : ImageFrame
AE --> CE : stats/diff
RE --> UI : ImageData ready
UI --> RE : "scale()"
IR <-- UI : "load()"
CM --> TS : prefetch
@enduml
```
