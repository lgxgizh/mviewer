#pragma once

#include "core/image/ImageBuffer.h"

#include <array>
#include <cstdint>
#include <memory>
#include <string>

// ─── Foundation types ───────────────────────────────────────────────────────

enum class RenderCommandType : uint8_t
{
    DrawImage,
    DrawOverlay,
    DrawHistogram,
    DrawSelection,
    DrawPixelMarker,
};

enum class RenderInterp : int
{
    Nearest,
    Bilinear,
    Bicubic,
    Lanczos
};

struct RenderSize
{
    int width = 0;
    int height = 0;
    bool isValid() const { return width > 0 && height > 0; }
};

struct RenderRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool isValid() const { return width > 0 && height > 0; }
};

// ─── Renderer interface ─────────────────────────────────────────────────────

class Renderer
{
public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;

    virtual ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode) = 0;
    virtual ImageData
    overlayDifference(const ImageData& base, const ImageData& diff, double alpha) = 0;
    virtual ImageData scaleRegion(const ImageData& src,
        const RenderRect& region,
        const RenderSize& target,
        RenderInterp mode) = 0;
};

class SoftwareRenderer : public Renderer
{
public:
    std::string backendName() const override { return "software"; }
    ImageData scale(const ImageData& src, const RenderSize& target, RenderInterp mode) override;
    ImageData
    overlayDifference(const ImageData& base, const ImageData& diff, double alpha) override;
    ImageData scaleRegion(const ImageData& src,
        const RenderRect& region,
        const RenderSize& target,
        RenderInterp mode) override;
};

// ─── RenderCommand ──────────────────────────────────────────────────────────

struct RenderCommand
{
    RenderCommandType type;
    RenderSize targetSize;
    RenderRect rect;
    int rgba = 0;
    double alpha = 1.0;
    int interp = 0;
    std::array<int, 256> histData{};
    int histCount = 0;

    ImageData srcImage;
    ImageData overlayImage;

    static RenderCommand drawImage(const ImageData& img, const RenderSize& tgt, RenderInterp m)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawImage;
        c.srcImage = img;
        c.targetSize = tgt;
        c.interp = static_cast<int>(m);
        return c;
    }
    static RenderCommand drawOverlay(const ImageData& img, double a)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawOverlay;
        c.overlayImage = img;
        c.alpha = a;
        return c;
    }
    static RenderCommand drawHistogram(const int* bins, int n, const RenderRect& r)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawHistogram;
        c.histCount = n;
        c.rect = r;
        for (int i = 0; i < n && i < 256; ++i)
            c.histData[i] = bins[i];
        return c;
    }
    static RenderCommand drawSelection(const RenderRect& r, int rgb)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawSelection;
        c.rect = r;
        c.rgba = rgb;
        return c;
    }
    static RenderCommand drawPixelMarker(int x, int y, int rgb)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawPixelMarker;
        c.targetSize = {x, y};
        c.rgba = rgb;
        return c;
    }
};

// ─── RenderEngine facade ────────────────────────────────────────────────────

class RenderEngine
{
public:
    static RenderEngine& instance();

    void setBackend(std::unique_ptr<Renderer> r);
    // Query the active backend name (useful for tests/logging).
    std::string backendName() const;

    ImageData scale(const ImageData& src,
        const RenderSize& target,
        RenderInterp mode = RenderInterp::Bilinear);
    ImageData overlayDifference(const ImageData& base, const ImageData& diff, double alpha = 0.5);
    ImageData scaleRegion(const ImageData& src,
        const RenderRect& region,
        const RenderSize& target,
        RenderInterp mode = RenderInterp::Bilinear);

    static ImageData scaleStatic(const ImageData& src,
        const RenderSize& target,
        RenderInterp mode = RenderInterp::Bilinear);
    static ImageData
    overlayDifferenceStatic(const ImageData& base, const ImageData& diff, double alpha = 0.5);
    static ImageData scaleRegionStatic(const ImageData& src,
        const RenderRect& region,
        const RenderSize& target,
        RenderInterp mode = RenderInterp::Bilinear);

private:
    RenderEngine();
    std::unique_ptr<Renderer> m_backend;
};
