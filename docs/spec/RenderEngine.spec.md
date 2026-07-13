# RenderEngine Specification

## Overview

RenderEngine provides image scaling and overlay operations. It is Qt-free at the header level; implementations may use Qt internally.

## API

```cpp
enum class InterpMode { Nearest, Bilinear, Bicubic, Lanczos };

struct RenderSize { int width = 0; int height = 0; };
struct RenderRect { int x=0, y=0, width=0, height=0; bool isValid() const; };

class RenderEngine {
public:
    // Scale entire image to target size
    static ImageData scale(const ImageData& src, const RenderSize& target,
                           InterpMode mode = InterpMode::Bilinear);

    // Scale a source region to target size
    static ImageData scaleRegion(const ImageData& src, const RenderRect& region,
                                 const RenderSize& target,
                                 InterpMode mode = InterpMode::Bilinear);

    // Overlay diff onto base with alpha blending
    static ImageData overlayDifference(const ImageData& base,
                                       const ImageData& diff, double alpha = 0.5);
};
```

## Input

| Parameter | Type | Range |
|-----------|------|-------|
| `src` | `ImageData` | Valid, non-null |
| `target` | `RenderSize` | width > 0, height > 0 |
| `region` | `RenderRect` | Clipped to src bounds |
| `mode` | `InterpMode` | Default Bilinear |
| `alpha` | `double` | 0.0 – 1.0 |

## Output

| Method | Return | Semantics |
|--------|--------|-----------|
| `scale` | `ImageData` | Scaled image or null on failure |
| `scaleRegion` | `ImageData` | Scaled region or null |
| `overlayDifference` | `ImageData` | Blended image or null |

## Performance

| Operation | Budget |
|-----------|--------|
| Bilinear scale (24MP → 1080p) | < 16ms |
| Bicubic scale (24MP → 1080p) | < 50ms |
| Overlay blend | < 5ms |

## Error Handling

- Null input → null output
- Zero target size → null output
- Region outside bounds → clipped automatically

## Future Backends

- `SoftwareRenderer` (current, Qt-based)
- `D2DRenderer` (Direct2D, Windows)
- `OpenGLRenderer` (cross-platform)
- `VulkanRenderer` (future)
