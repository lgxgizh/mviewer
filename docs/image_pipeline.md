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
| -------- | ------------ | -------- |
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

[unverified] — this API does not exist in the codebase: there is no `ImageFormat`
enum, no `detectFormat()` and no `FormatDetectionResult` in `src/`. Format
dispatch happens through `DecoderRegistry::canDecode()` (each decoder claims by
extension or content), driven by the signatures above. The block below is a
design sketch, not implemented code:

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
// src/core/image/decoder/IDecoder.h — Qt-free header.
class IDecoder {
public:
    virtual ~IDecoder() = default;

    /// True if this decoder claims the given file (by extension or content).
    virtual bool canDecode(const std::string& path) const = 0;

    /// Full-resolution decode -> RGB24 ImageData (null buffer on failure).
    virtual ImageData decodeFull(const std::string& path) const = 0;

    /// Scaled decode: longest edge clamped to maxEdge, aspect ratio preserved.
    virtual ImageData decodeScaled(const std::string& path, int maxEdge) const = 0;

    /// Presentation consumers can take source metadata from the same reader pass.
    virtual ImageData decodeScaled(const std::string& path, int maxEdge,
                                   mviewer::domain::ImageMetadata& outMeta) const;

    /// Decode + metadata in one pass.
    virtual ImageData decodeFull(const std::string& path,
                                 mviewer::domain::ImageMetadata& outMeta) const = 0;

    /// Lowercased extensions this decoder handles (e.g. "jpg", "png").
    virtual std::vector<std::string> extensions() const = 0;

    /// Human-readable name (for diagnostics).
    virtual const char* name() const = 0;
};
```

Failure is reported by a null `ImageData`, never by an exception or an error
code: there is no `DecodeError` type and no `std::expected` return. Decoders are
selected through `DecoderRegistry`, not called directly by the UI.

### Decoder Registry

Decoders are registered with `DecoderRegistry`
(`src/core/image/decoder/DecoderRegistry.h`); the registry dispatches a file to
the first decoder whose `canDecode()` returns true, and the fallback is
registered last. No per-format codec library is linked (there is no
libjpeg-turbo / libheif / libjxl / WIC dependency):

| Decoder | Role |
| -------- | ------ |
| `QtDecoder` | Primary path — decodes through Qt's image reader for every format that Qt build supports |
| `QtFallbackDecoder` | Registered last: the last-resort path when no specific decoder claims the file |
| `RawDecoder` | RAW files (CR2/CR3/NEF/ARW/DNG/ORF/RW2/PEF/RAF…): serves the embedded preview, and the 16-bit samples used by the Pixel Inspector |
| plugin decoders | Any `IDecoder` a plugin registers (`PluginManager` → `DecoderRegistry::registerDecoder`), e.g. the shipped PPM example |

The registry holds decoders behind a mutex and decodes over a snapshot of that
list, so a plugin can be unloaded mid-decode without invalidating a running
decode.

### Fallback Chain

```
Try primary decoder
    │
    ├── Success → return ImageData
    │
    └── Failure (null buffer) → Try fallback decoder
                    │
                    ├── Success → return ImageData
                    │
                    └── Failure → Load fails: `ImageRepository::Result::error` explains why
```

---

## Stage 3: Decoding

### Decode Parameters

There is no `DecodeParams` struct: the caller's options and the decoder's own
`maxEdge` argument are the whole parameter set.

```cpp
// src/core/image/ImageRepository.h
struct ImageLoadOptions {
    bool useDiskCache = true;      // consult / populate the disk tier
    bool generateHistogram = true;
    int maxEdgeForThumbnail = 256; // longest edge requested for the thumbnail tier
    int frameMaxEdge = 0;          // 0 = native / full frame (M57 frame prefetch)
};
```

`IDecoder` takes only a path, plus `maxEdge` for the scaled tier:
`decodeFull(path)` / `decodeScaled(path, maxEdge)`, each with an optional
`mviewer::domain::ImageMetadata&` overload that fills source metadata from the
same reader pass.

### Decode Output (current M35 contract)

`ImageFrame` contains an `ImageData` analysis buffer plus `ImageMetadata`.
Embedded ICC bytes are retained in the frame metadata sidecar under the
reserved key `MViewer.DisplayICC.Base64`; no Qt type crosses the domain/core
header boundary.

### Pixel Format Strategy

- **Default output:** `RGBA8` (8-bit per channel, premultiplied alpha)
- **HDR images:** `RGB16F` (16-bit float) — future
- **Grayscale:** `Gray8` — converted to RGBA8 for display
- **CMYK:** Converted to RGB during decode

### Color Space Handling

1. Decode and orient the image into 8-bit `ImageData`. These numeric values are
   the analysis domain used by Pixel Inspector, Histogram, Diff, PSNR, SSIM and ROI.
2. Retain a valid embedded ICC profile in the metadata sidecar; never rewrite
   the analysis buffer for display color management.
3. `mvcore::toDisplayQImage` materializes a display-only copy, applies the
   embedded source profile, and converts that copy to sRGB. Invalid or missing
   profiles deterministically fall back to sRGB-assumed values.
4. Thumbnail workers convert before square-fit and persist display-ready PNGs
   (`ThumbnailCache` schema 4 — the display-ready, ICC-converted payload).
   Preview scaled decodes carry source dimensions,
   file identity and profile metadata from the same decoder pass; cached
   previews are display-ready. ImageViewer CPU tiles and GPU uploads share the
   same display-ready tile materialization, so repaint does not repeat ICC work.
5. Compare panes use the same display materializer. Display conversion is not
   used by export/analysis paths unless their contract explicitly requests it.

### Compare source truth versus display LOD

Compare has two deliberately separate representations:

- `ImageFrame::pixels()` / `ImageData` is the source of truth for Pixel
  Inspector RGB, 1×1/3×3/5×5/7×7 neighborhoods, ROI histograms, Diff, PSNR,
  SSIM and report/export inputs. Inspector sampling maps the adjusted-pane
  coordinate back through crop and rotation, then applies point adjustments
  without constructing a full-resolution `QImage`.
- `RawImageView::image()` is a bounded, viewport-oriented display LOD. It is
  used for painting and interaction geometry only. Its nonlinear preview
  operations (gamma and clipping in particular) are an approximation and must
  never become analysis input.
- ICC conversion remains display-only. An ICC profile can change the rendered
  display pixel without changing the numeric source sample used by analysis.
- An identity, untransformed source with an available RAW16 plane may expose
  the exact 16-bit sample. Adjusted/cropped/rotated samples are reported from
  the adjusted 8-bit analysis path and are labeled as such in the Inspector.

### EXIF Orientation

- Read orientation tag during decode
- Apply rotation/flip to pixel data during decode
- Never store oriented pixels in cache
- Orientation is "baked in" to cached decoded image

---

## Stage 4: Post-Processing

### Operations

| Operation | When | Where |
| ----------- | ------ | ------- |
| Color space conversion | Worker/display-cache materialization | Qt color pipeline |
| EXIF orientation | During decode | CPU (SIMD) |
| Demosaicing (RAW) | N/A — RAW out of scope | — |
| Resize (thumbnail) | After decode | CPU (SIMD) or GPU |
| Sharpening (optional) | After resize | GPU (shader) |

### Resize Quality

| Use Case | Algorithm |
| ---------- | ----------- |
| Thumbnail generation | Lanczos3 or bilinear |
| On-screen zoom (downscale) | GPU bilinear/trilinear |
| On-screen zoom (upscale) | GPU bilinear |

---

## Stage 5: Caching

### Cache Insertion

After decode, the pixels are written to the `FullImage` memory pool with
`CacheManager::putMemory(CacheLevel::FullImage, key, pixels)` and persisted
through `DiskCache` (SQLite blobs); the thumbnail tier is written by the
thumbnail workers. See `docs/cache.md` for the real hierarchy.

### Cache Key

Every tier is keyed by one `std::string`: the file-identity key built by
`ImageRepository::makeKey()` from `MetadataReader::key(path)`, i.e.
`path|fileSize|mtimeMsec` (fields separated so distinct files cannot collide).
A frame load uses `ImageRepository::makeFrameKey(path, frameIndex,
decodeVariant)`, which extends the same identity with the frame index and decode
variant. There is no `CacheKey` struct — a key is a plain string, and mtime changes
invalidate it.

### Invalidation

- File modification time changed → the key changes, so the old entry is no longer reachable
- File size changed → same, both fields are part of the key
- File deleted or overwritten → `ImageRepository::release()` / `invalidate()` calls `CacheManager::invalidate()`, which drops the key from every tier (pixels, metadata, disk)
- Cache entry evicted by LRU → each memory pool and the disk cache enforce their own budget

---

## Stage 6: GPU Upload

### Texture Creation

[unverified] — no `uploadToGpu()` and no `DecodedImage` type exist in the
codebase; GPU uploads live in the viewer (Qt/OpenGL), not in `core/`. The
signature below is a design sketch.

```cpp
struct TextureHandle { uint64_t id; };

auto uploadToGpu(const DecodedImage& image) -> TextureHandle;
```

### Texture Format Mapping

| Decoded Format | GPU Format (D3D11) | GPU Format (OpenGL) |
| ---------------- | ------------------- | --------------------- |
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

[unverified] — there is no `AnimationInfo` / `FrameInfo` / `DisposalMethod` type
in the codebase. Multi-frame files are handled through `FrameSequence`
(`core/image/FrameSequence.h`) and
`ImageRepository::loadFrame(path, frameIndex, opts)`. The structure below is a
design sketch.

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
| ----------- | --------- | ------- |
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
| ------- | ------- | ---------- |
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
| ----------- | -------- |
| Format detection | < 1 ms |
| Decoder selection | < 0.1 ms |
| JPEG decode (24MP) | < 50 ms |
| PNG decode (24MP) | < 100 ms |
| Thumbnail decode (256px) | < 20 ms |
| GPU upload (24MP RGBA8) | < 10 ms |
| Total pipeline (preloaded) | < 16 ms |
