# ADR 031 — Range-check the reader image format; keep RenderEngine alive at exit

Date: 2026-11-01
Status: Accepted

## Context

The 2026-11-01 Nightly found two sanitizer failures:

- clang-cl UBSan trapped (load-invalid-value) in
  `QtDecoder::decodeFullResult`. `QImageReader::imageFormat()` returned
  `0xBAADF00D`: an image plugin answered `QImageIOHandler::ImageFormat` with
  an uninitialized value, and storing that in a `QImage::Format` is undefined.
- GCC ASan reported a SEGV in `RenderEngine::scaleRegion`. A viewer tile
  worker was still running while static destructors destroyed the
  function-local `RenderEngine`, so `m_backend` was already null.

## Decision

- Core code never stores `QImageReader::imageFormat()` directly. It calls
  `qtmetadata::readerImageFormat()` (`QtMetadataSemantics.h`), which reads the
  value as `int` and maps anything outside `QImage::Format` to
  `Format_Invalid`. This applies to `ImageRepository_load.cpp`, the Qt
  decoders, `FrameSequence.cpp` and `MetadataReader.cpp`.
- `RenderEngine::instance()` is an intentionally immortal heap object. Every
  backend call checks for a missing backend and returns an empty result.

## Consequences

A plugin that reports garbage gives an unknown source format instead of
undefined behaviour. Work that runs during process exit can still render.
The engine is never freed, but it stays reachable, so LeakSanitizer does not
report it.
