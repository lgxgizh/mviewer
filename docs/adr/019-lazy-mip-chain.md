# ADR 019 — Lazy in-memory mip chain

## Context

`CacheManager::putMemory(FullImage)` built the whole power-of-two mip chain
synchronously on every put. The 2026-10 performance review measured ~12–22 ms
for a 12 MP frame and ~117 ms plus ~100 MB extra for a 100 MP frame, paid
between decode and first paint even for images only viewed at full size.

## Decision

- A FullImage put only drops any stale chain for that key.
- `CacheManager::getBestMip` builds the chain the first time a level smaller
  than the full image is requested (this lazy path already existed).
- `ensureMips` / `ensureAndBestMip` remain for callers that want the chain now.

## Consequences

- Opening a large image no longer pays for mips up front; RAM for mips is only
  spent when a reduced level is actually used (zoom-out, compare fit, tiles).
- The first reduced-level request pays the build cost once, on the worker that
  asked for it.
