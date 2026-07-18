# ADR 005: Unified CacheManager over memory + disk

## Status

Accepted

## Context

We have three memory tiers (Thumbnail / Preview / FullImage, each LRU) and a
SQLite-backed disk cache. Callers should not juggle two subsystems.

## Decision

`core/cache/CacheManager` is the single cache facade. Memory levels delegate
to `ImageCache` (per-level LRU pools); the `Disk` level delegates to
`DiskCache` (SQLite keyed by file identity: path+size+mtime). `get()` tries
memory first, then falls back to disk and promotes into memory.

## Consequences

- One coherent API (`get`/`put`/`clear` per `CacheLevel`).
- Existing `ImageCache`/`DiskCache` implementations are reused untouched.
- Future: per-level size accounting and predictive `prefetch()`.
