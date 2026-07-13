# ADR 002: Use ImageBuffer/ImageData, not QImage, in the core

## Status
Accepted

## Context
The core (decode, cache, compare, analyze) must run without a GUI and be
testable headless. `QImage` pulls in QtGui and implies a specific pixel
layout/format model that is awkward for raw decode buffers.

## Decision
Core pixel containers are `ImageBuffer` (non-owning view: `data`, `width`,
`height`, `PixelFormat`) and `ImageData` (owning buffer with shared
`std::shared_ptr<uint8_t[]>`). Conversion to/from `QImage` happens only at
the boundary via `core/image/QtConvert.h` (`mvcore::toQImage` /
`mvcore::fromQImage`).

## Consequences
- Core headers are Qt-free and compile standalone.
- Format is explicit (`RGB24`/`RGBA32`/`BGR24`/`BGRA32`/`Grayscale8`),
  avoiding silent `QImage` format conversions.
