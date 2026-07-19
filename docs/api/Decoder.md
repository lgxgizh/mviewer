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
| Format | Decode | Scaled decode | Plugin / backend |
|--------|--------|---------------|------------------|
| JPEG   | ✅ | ✅ | Qt QtGui (built-in) |
| PNG    | ✅ | ✅ | Qt QtGui (built-in) |
| BMP    | ✅ | ✅ | Qt QtGui (built-in) |
| TIFF   | ✅ | ✅ | `qtiff.dll` (qtimageformats module) — **G1**: must ship in portable zip / installer |
| WebP   | ✅* | ✅* | `qwebp.dll` (qtimageformats module) |

\* if the Qt imageformats module is deployed (it is, via windeployqt in the
pack scripts + G1 guard).

## Thread-safety
`Decoder` static methods are stateless and safe to call from multiple worker
threads concurrently (each call owns its own decode state).

## Error contract
Returns empty `ImageData` on failure; never throws across the public API.

## Status
✅ Stable. No change planned for M12. (Large RAW / 100MP decode is a post-1.0
tile-pipeline item per roadmap — see Tile Render RFC, deferred.)
