# API — Decoder

**Header**: `src/core/image/Decoder.h`
**Layer**: core (Qt-free; per-format logic in `core/image/decoder/*`)

## Purpose
Stateless decode entry point. All real decode logic lives in per-format decoder
modules under `core/image/decoder/`; `Decoder` is the unified facade selected by
file extension / magic bytes.

## Interface (contract)
```cpp
namespace mviewer::core {

class Decoder {
public:
    // Full-resolution decode.
    static ImageData decodeFull(const std::string &path);
    // Decode scaled so the longest edge <= maxEdge (thumbnail/preview path).
    static ImageData decodeScaled(const std::string &path, int maxEdge);
    // Full decode that also fills out the domain metadata (dimensions, etc.).
    static ImageData decodeFull(const std::string &path,
                                mviewer::domain::ImageMetadata &outMeta);
};

} // namespace mviewer::core
```

## Format support matrix
| Format | Static decode | Sequence capability | Plugin / backend |
|--------|---------------|---------------------|------------------|
| JPEG   | ✅ | one static frame | Qt QtGui (built-in) |
| PNG    | ✅ | one static frame | Qt QtGui (built-in) |
| BMP    | ✅ | one static frame | Qt QtGui (built-in) |
| TIFF   | ✅ | ✅ pages | `qtiff.dll` (qtimageformats module) — must ship in portable zip / installer |
| GIF    | ✅* | ✅* animation | `qgif.dll` (Qt imageformats module) |
| WebP   | ✅* | ✅* animation | `qwebp.dll` (Qt imageformats module) |

\* if the Qt imageformats module is deployed (it is, via windeployqt in the
pack scripts + G1 guard).

## Sequence capability

`src/core/image/FrameSequence.h` is the Qt-free core contract for multi-frame
sources:

```cpp
FrameSequenceInfo probeSequence(const std::string &path);
FrameDecodeResult decodeFrame(const std::string &path, int frameIndex);
FrameDecodeResult decodeFrameScaled(const std::string &path, int frameIndex,
                                    int maxEdge);
```

`FrameSequenceKind::Animation` is used for GIF/WebP and
`FrameSequenceKind::Pages` for multi-page TIFF. `FrameIdentity` adds the
frame/page index and decode variant to the existing file-revision key. The
implementation uses sequential fallback for qgif/qwebp because the qualified
Qt 6.10 runtime reports counts but does not make `jumpToImage()` reliable for
those handlers. Decode errors return an unsuccessful result and do not throw
across the core/application boundary.

## Thread-safety
`Decoder` static methods are stateless and safe to call from multiple worker
threads concurrently (each call owns its own decode state).

## Error contract
Returns empty `ImageData` on failure; never throws across the public API.

## Status
✅ Stable. No change planned for M12. (Large RAW / 100MP decode is a post-1.0
tile-pipeline item per roadmap — see Tile Render RFC, deferred.)
