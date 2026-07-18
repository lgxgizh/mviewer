# ADR 009: Domain layer as Qt-free value types

## Status

Accepted

## Context

Business concepts (image identity, metadata, histogram, ROI selection,
compare session) should be stable, portable, and testable in isolation.

## Decision

`domain/` holds plain structs/enums only: `Image` (metadata, `ImageId`,
`PixelCoord`, `PixelColor`), `Histogram`, `Selection`, `CompareSession`.
No Qt includes, no implementation files. Namespaced `mviewer::domain`.

## Consequences

- Domain compiles standalone and is reusable by any layer/test.
- Core can depend on domain without taking a Qt dependency.
- Keeps the inward dependency rule enforceable.
