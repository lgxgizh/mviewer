# ADR 008: ImageRepository (repository pattern)

## Status
Accepted

## Context
Callers should not coordinate FileSystem + Decoder + DiskCache + histogram
computation by hand; that logic is easy to get wrong and hard to test.

## Decision
`core/image/ImageRepository` is the abstraction over the image lifecycle:
`load()` (sync, with disk-cache lookup + decode + histogram), `loadAsync()`
(dispatched via `TaskScheduler`), and `loadDirectory()` (batch). It returns a
`std::shared_ptr<ImageFrame>` so all engines share one runtime image type.

## Consequences
- Single entry point for "give me an image".
- Engines operate uniformly on `ImageFrame`.
- Async path keeps the UI responsive.
