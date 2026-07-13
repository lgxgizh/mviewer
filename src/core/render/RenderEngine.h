#pragma once

#include "core/image/ImageBuffer.h"

#include <string>

// Render backend abstraction: Qt-independent interface.
// All backends scale/overlay without QWidget dependency; future D2D/OpenGL/Vulkan pluggable.

enum class InterpMode : int { Nearest, Bilinear, Bicubic, Lanczos };

struct RenderSize {
    int width = 0;
    int height = 0;
    bool isValid() const { return width > 0 && height > 0; }
};

struct RenderRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool isValid() const { return width > 0 && height > 0; }
};

class Renderer {
public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;

    // High-quality scale: src → target size with given interpolation.
    virtual ImageData scale(const ImageData& src, const RenderSize& target,
                            InterpMode mode) = 0;

    // Overlay diff image (grayscale heatmap) onto base with alpha blending.
    virtual ImageData overlayDifference(const ImageData& base,
                                        const ImageData& diff, double alpha) = 0;

    // Scale a sub-region of src → target size.
    virtual ImageData scaleRegion(const ImageData& src, const RenderRect& region,
                                  const RenderSize& target,
                                  InterpMode mode) = 0;
};

// Software renderer implementation (Qt-backed, current default).
class SoftwareRenderer : public Renderer {
public:
    std::string backendName() const override { return "software"; }
    ImageData scale(const ImageData& src, const RenderSize& target,
                    InterpMode mode) override;
    ImageData overlayDifference(const ImageData& base,
                                const ImageData& diff, double alpha) override;
    ImageData scaleRegion(const ImageData& src, const RenderRect& region,
                          const RenderSize& target, InterpMode mode) override;
};

// RenderEngine: facade over the current backend.
// Holds a pluggable Renderer; defaults to SoftwareRenderer.
class RenderEngine {
public:
    static RenderEngine& instance();

    // Swap renderer backend (e.g., D2DRenderer later). nullptr → restore default.
    void setBackend(std::unique_ptr<Renderer> r);

    // Convexience wrappers (no-static; forward to backend).
    ImageData scale(const ImageData& src, const RenderSize& target,
                    InterpMode mode = InterpMode::Bilinear);
    ImageData overlayDifference(const ImageData& base,
                                const ImageData& diff, double alpha = 0.5);
    ImageData scaleRegion(const ImageData& src, const RenderRect& region,
                          const RenderSize& target,
                          InterpMode mode = InterpMode::Bilinear);

    // Legacy static APIs kept for backward compatibility.
    static ImageData scaleStatic(const ImageData& src, const RenderSize& target,
                                 InterpMode mode = InterpMode::Bilinear);
    static ImageData overlayDifferenceStatic(const ImageData& base,
                                             const ImageData& diff, double alpha = 0.5);
    static ImageData scaleRegionStatic(const ImageData& src, const RenderRect& region,
                                       const RenderSize& target,
                                       InterpMode mode = InterpMode::Bilinear);

private:
    RenderEngine();
    std::unique_ptr<Renderer> m_backend;
};
