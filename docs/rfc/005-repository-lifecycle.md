# RFC-005: ImageRepository Lifecycle

## Status

Implemented

## Priority

P0

## Goal

ImageRepository becomes the single lifecycle manager for images.

## Responsibilities

```
Directory Scan
    ↓
Metadata
    ↓
Image Object (ImageFrame)
    ↓
Decoder
    ↓
Cache
    ↓
Release
```

## Rules

- Repository owns image lifetime
- Other modules should not directly create ImageFrame
- Repository decides when to load, cache, and release
- All cache operations go through repository

## Migration

1. Add scanner (filesystem watcher)
2. Add metadata cache layer
3. Repository exposes: load(), loadAsync(), prefetch(), release()
4. Update existing consumers to use ImageFrame
