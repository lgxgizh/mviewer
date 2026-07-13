# MViewer Image Pipeline Specification

## Overview

The image pipeline transforms a file path into pixels on screen. It encompasses format detection, decoding, color management, caching, and rendering. The pipeline is designed for maximum throughput with minimum latency.

---

## Pipeline Stages

```
┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐    ┌──────────┐
│  Format  │───▶│  Decoder │───▶│  Color   │───▶│  Cache   │───▶│  Render  │
│ Detection│    │  Select  │    │  Process │    │  Insert  │    │  Upload  │
└──────────┘    └──────────┘    └──────────┘    └──────────┘    └──────────┘
```

---

## Stage 1: Format Detection

### Strategy

1. **Magic bytes** (primary) — Read first 16 bytes, match against known signatures
2. **File extension** (fallback) — Used only if magic bytes are ambiguous
3. **Content sniffing** (last resort) — Probe decoders in priority order

### Magic Byte Signatures

| Format | Magic Bytes | Offset |
|--------|------------|--------|
| JPEG | `FF D8 FF` | 0 |
| PNG | `89 50 4E 47 0D 0A 1A 0A` | 0 |
| BMP | `42 4D` | 0 |
| GIF | `47 49 46 38` (GIF87a/GIF89a) | 0 |
| TIFF (LE) | `49 49 2A 00` | 0 |
| TIFF (BE) | `4D 4D 00 2A` | 0 |
| WebP | `52 49 46 46` .... `57 45 42 50` | 0, 8 |
| AVIF | `66 74 79 70 61 76 69 66` (ftypavif) | 4 |
| HEIC | `66 74 79 70 68 65 69 63` (ftypheic) | 4 |
| JPEG XL | `FF 0A` or `00 00 00 0C 4A 58 4C 20 0D 0A 87 0A` | 0 |

### Detection API

```cpp
enum class ImageFormat {
    Unknown,
    JPEG, PNG, BMP, GIF, TIFF,
    WebP, AVIF, HEIF, JXL,
};

struct FormatDetectionResult {
    ImageFormat format;
    float confidence;  // 0.0 - 1.0
};

auto detectFormat(std::span<const std::byte, 16> header) 
    -> FormatDetectionResult;
```

---

## Stage 2: Decoder Selection

### Decoder Interface

```cpp
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /// Check if this decoder can handle the given format
    virtual bool canDecode(ImageFormat format) const = 0;

    /// Full-resolution decode
    virtual std::expected<DecodedImage, DecodeError>
        decode(const FilePath& path, const DecodeParams& params) = 0;

    /// Thumbnail-resolution decode (fast path)
    virtual std::expected<DecodedImage, DecodeError>
        decodeThumbnail(const FilePath& path, int maxEdgeLength) = 0;

    /// Animated image support
    virtual bool supportsAnimation() const { return false; }
    virtual std::expected<AnimationInfo, DecodeError>
        queryAnimation(const FilePath& path) { return std::unexpected(...); }
};
```

### Decoder Factory

The factory maintains a priority-ordered list of decoders. For each format, multiple decoders may be registered (primary + fallback).

| Format | Primary Decoder | Fallback Decoder |
|--------|----------------|------------------|
| JPEG | libjpeg-turbo | WIC |
| PNG | libpng | WIC |
| BMP | Custom | — |
| GIF | libvips | Custom |
| TIFF | libtiff | WIC |
| WebP | libwebp | WIC |
| AVIF | libdav1d + libaom | WIC |
| HEIF/HEIC | libheif | WIC |
| JPEG XL | libjxl | — |

### Fallback Chain

```
Try primary decoder
    │
    ├── Success → return DecodedImage
    │
    └── Failure → Try fallback decoder
                    │
                    ├── Success → return DecodedImage
                    │
                    └── Failure → Return DecodeError::UnsupportedFormat
```

---

## Stage 3: Decoding

### Decode Parameters

```cpp
struct DecodeParams {
    std::optional<Resolution> maxResolution;  // Downscale during decode
    ColorSpace targetColorSpace = ColorSpace::sRGB;
    bool applyOrientation = true;             // EXIF orientation
    bool flattenAnimation = true;             // Return first frame only
    Orientation orientation = Orientation::Identity;
};
```

### Decode Output

```cpp
struct DecodedImage {
    int width;
    int height;
    PixelFormat format;           // RGBA8, RGB16, etc.
    std::vector<std::byte> data;  // Pixel buffer
    ColorSpace colorSpace;
    std::optional<IccProfile> iccProfile;
};
```

### Pixel Format Strategy

- **Default output:** `RGBA8` (8-bit per channel, premultiplied alpha)
- **HDR images:** `RGB16F` (16-bit float) — future
- **Grayscale:** `Gray8` — converted to RGBA8 for display
- **CMYK:** Converted to RGB during decode

### Color Space Handling

1. Read ICC profile from file metadata (if present)
2. If no ICC profile, assume sRGB
3. Convert to sRGB for display
4. Store original ICC profile for metadata panel display

### EXIF Orientation

- Read orientation tag during decode
- Apply rotation/flip to pixel data during decode
- Never store oriented pixels in cache
- Orientation is "baked in" to cached decoded image

---

## Stage 4: Post-Processing

### Operations

| Operation | When | Where |
|-----------|------|-------|
| Color space conversion | During decode | CPU (SIMD) |
| EXIF orientation | During decode | CPU (SIMD) |
| Demosaicing (RAW) | N/A — RAW out of scope | — |
| Resize (thumbnail) | After decode | CPU (SIMD) or GPU |
| Sharpening (optional) | After resize | GPU (shader) |

### Resize Quality

| Use Case | Algorithm |
|----------|-----------|
| Thumbnail generation | Lanczos3 or bilinear |
| On-screen zoom (downscale) | GPU bilinear/trilinear |
| On-screen zoom (upscale) | GPU bilinear |

---

## Stage 5: Caching

### Cache Insertion

After decode, the image is inserted into the L2 (decoded image) cache. If the image is the current display target, it is also uploaded to the GPU (L1 texture cache).

### Cache Key

```cpp
struct CacheKey {
    FilePath path;
    int64_t fileSize;
    int64_t modificationTime;
    DecodeParams params;  // Orientation, resolution limit
};
```

### Invalidation

- File modification time changed → invalidate
- File size changed → invalidate
- File deleted → invalidate
- Cache entry evicted by LRU → remove

---

## Stage 6: GPU Upload

### Texture Creation

```cpp
struct TextureHandle { uint64_t id; };

auto uploadToGpu(const DecodedImage& image) -> TextureHandle;
```

### Texture Format Mapping

| Decoded Format | GPU Format (D3D11) | GPU Format (OpenGL) |
|----------------|-------------------|---------------------|
| RGBA8 | `DXGI_FORMAT_R8G8B8A8_UNORM` | `GL_RGBA8` |
| RGB8 | `DXGI_FORMAT_R8G8B8A8_UNORM` (pad) | `GL_RGBA8` (pad) |
| Gray8 | `DXGI_FORMAT_R8_UNORM` | `GL_R8` |

### Upload Strategy

- Upload occurs on the render thread (GPU context is thread-bound)
- Staging buffer for async upload (D3D11) or PBO (OpenGL)
- Texture remains in GPU cache until evicted

---

## Animation Pipeline

### Supported Formats

- GIF (required)
- WebP (animated, required)
- APNG (future)

### Animation Info

```cpp
struct AnimationInfo {
    int frameCount;
    int loopCount;  // 0 = infinite
    std::vector<FrameInfo> frames;
};

struct FrameInfo {
    int width;
    int height;
    int durationMs;  // Frame delay
    DisposalMethod disposal;  // Background, Previous, None
};
```

### Playback

1. Decode all frames on background thread (or lazily)
2. Store frames in animation cache
3. UI timer advances to next frame at specified interval
4. Composite frame onto canvas using disposal method
5. Loop or stop based on loop count

---

## Preloading Pipeline

### Prediction Model

```
User navigates forward 3 times consecutively
    │
    ▼
Predictor: "User is browsing forward"
    │
    ▼
Preload next 3 images in forward direction
    │
    ▼
Enqueue decode tasks with HIGH priority
```

### Preload Configuration

| Parameter | Default | Range |
|-----------|---------|-------|
| Forward preload count | 3 | 0-10 |
| Backward preload count | 1 | 0-5 |
| Preload trigger threshold | 2 consecutive same-direction navigations | 1-5 |
| Max concurrent preloads | 2 | 1-4 |

### Priority Queue

```
Priority 1: Current image (display now)
Priority 2: Visible thumbnails
Priority 3: Forward preloads
Priority 4: Backward preloads
Priority 5: Background thumbnail generation
```

---

## Error Handling

### Decode Errors

| Error | Cause | Recovery |
|-------|-------|----------|
| `FileNotFound` | Path doesn't exist | Show error, skip to next |
| `InvalidFormat` | Magic bytes don't match extension | Try fallback decoder |
| `CorruptData` | File truncated or damaged | Show partial decode if possible |
| `UnsupportedFormat` | No decoder available | Show "unsupported" placeholder |
| `OutOfMemory` | Image too large | Try thumbnail-resolution decode |
| `PermissionDenied` | Access denied | Show error, skip |

### Graceful Degradation

1. Try full-resolution decode
2. On failure, try thumbnail-resolution decode
3. On failure, show placeholder with error icon
4. Log error for debugging

---

## Performance Targets

| Operation | Target |
|-----------|--------|
| Format detection | < 1 ms |
| Decoder selection | < 0.1 ms |
| JPEG decode (24MP) | < 50 ms |
| PNG decode (24MP) | < 100 ms |
| Thumbnail decode (256px) | < 20 ms |
| GPU upload (24MP RGBA8) | < 10 ms |
| Total pipeline (preloaded) | < 16 ms |
