# ADR-002: Why ImageData (not QImage) as the Core Image Object

## Status

Accepted

## Context

Qt's `QImage` is the natural choice for pixel storage, but it ties the core layer to Qt. The core layer must remain Qt-free per RFC-010 (Domain First).

## Decision

Introduce **`ImageData`** (a Qt-free struct with `shared_ptr<uint8_t[]>`, width, height, format) as the core pixel container. `QImage` is used only at the UI boundary via `QtConvert`.

## Rationale

- **Qt-free core** — `ImageData` contains no Qt types; core algorithms don't link to QtGui
- **Explicit ownership** — `shared_ptr` makes lifetime clear; no implicit QImage detach/copy-on-write surprises
- **Format-aware** — `PixelFormat` enum (RGB24/RGBA32/BGR24/BGRA32/Grayscale8) covers all needed layouts
- **Zero-copy boundary** — `QtConvert::toQImage()` wraps `ImageData` into `QImage` without copying pixels (via `QImage::fromData` or const-cast wrapping)
- **Future GPU** — a plain buffer is easier to upload to GPU textures than QImage's internal format

## Conversions

- `QtConvert::fromQImage(QImage) → ImageData` — UI → Core
- `QtConvert::toQImage(ImageData) → QImage` — Core → UI

## Consequences

- ✅ Core layer compiles without Qt
- ✅ Algorithms operate on raw buffers (SIMD-friendly)
- ❌ Need explicit conversion at UI boundary
- ❌ Slightly more code than using QImage everywhere

## Related

- RFC-002 (ImageFrame unified domain object)
- RFC-010 (Domain First)
