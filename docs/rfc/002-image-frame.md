# RFC-002: Image Domain Model (ImageFrame)

## Status
Draft → Accepted

## Priority
P0

## Goal
Introduce **ImageFrame** as the unified domain object that owns everything related to one image.

## Current State
`ImageData` only stores pixels. Engines pass around ImageData + metadata + histogram + state separately.

## Target State
```
ImageFrame
    Pixel Buffer (ImageData)
    Metadata (ImageMetadata)
    Histogram Cache (Histogram)
    Thumbnail (ImageData, future)
    Decode State (DecodeState)
    Cache State (CacheState)
    File Information (path, size, mtime)
    Image Id (ImageId)
```

## Requirements

### Input
- File path (for lazy load)
- Or: ImageData + metadata (for immediate construction)

### Output
- Single object passed between all engines
- All engines communicate through ImageFrame, not individual structures

### Ownership
- ImageRepository creates ImageFrame
- Consumers receive shared_ptr<const ImageFrame> or const ref
- ImageFrame is immutable after construction (except cache/decode state)

### Thread Safety
- ImageFrame itself is read-only after construction
- Cache/decode state changes are atomic or behind mutex

### Performance
- Histogram computed lazily (first access)
- Zero-copy: ImageData uses shared_ptr<byte[]>

### Error
- Invalid ImageFrame: isValid() returns false
- Decode failure: decodeState == Failed

## Benefits
- Less duplicated state
- Easier caching
- Easier synchronization
- Future GPU support (add gpuTexture field)

## Migration
1. Add thumbnail field to ImageFrame
2. Update all engine APIs to take ImageFrame
3. Remove ImageObject wrapper (or keep as compat shim)
4. Update tests

## Tests
- ImageFrame::create() fills metadata
- computeHistogram() populates histogram
- isValid() reflects decode state
- Thread-safe concurrent reads
