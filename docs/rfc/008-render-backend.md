# RFC-008: Render Backend Abstraction

## Status

Implemented

## Priority

P1

## Goal

RenderEngine supports backend abstraction. Software renderer now; Direct2D/OpenGL/Vulkan later.

## Interface

```cpp
class Renderer {
public:
    virtual ~Renderer() = default;
    virtual ImageData scale(const ImageData& src, const RenderSize& target, InterpMode mode) = 0;
    virtual ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha) = 0;
    virtual ImageData scaleRegion(const ImageData& src, const RenderRect& region, const RenderSize& target, InterpMode mode) = 0;
};

class SoftwareRenderer : public Renderer { /* Qt-based, current */ };
// Future: D2DRenderer, OpenGLRenderer, VulkanRenderer
```

## Rule

RenderEngine should never directly depend on QWidget. UI only provides render target.

## Consequences

- Backend-swappable rendering
- No QWidget dependency in render path
- Future GPU acceleration ready
