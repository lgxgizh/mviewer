#pragma once

#include "core/image/ImageBuffer.h"

// QPainter is used only by reference in method signatures below; a forward
// declaration keeps this core header Qt-free per the architecture boundary
// (no <QWidget>/<QPainter>/<QImage> in src/core/**/*.h). The full type is
// included in RenderEngine.cpp where the painting is implemented.
class QPainter;
class QRect;

#include <array>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <vector>

// ─── Foundation types ───────────────────────────────────────────────────────

enum class RenderCommandType : uint8_t
{
    DrawImage,
    DrawOverlay,
    DrawHistogram,
    DrawSelection,
    DrawHeatmap,
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
    bool isValid() const
    {
        return width > 0 && height > 0;
    }
};

struct RenderRect
{
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
    bool isValid() const
    {
        return width > 0 && height > 0;
    }
};

// ─── Renderer interface ─────────────────────────────────────────────────────

class Renderer
{
  public:
    virtual ~Renderer() = default;
    virtual std::string backendName() const = 0;

    virtual ImageData scale(const ImageData &src, const RenderSize &target, RenderInterp mode) = 0;
    virtual ImageData overlayDifference(const ImageData &base, const ImageData &diff,
                                        double alpha) const = 0;
    virtual ImageData scaleRegion(const ImageData &src, const RenderRect &region,
                                  const RenderSize &target, RenderInterp mode) = 0;
    virtual ImageData heatMap(const ImageData &gray, const RenderRect &rect) const = 0;
};

class SoftwareRenderer : public Renderer
{
  public:
    std::string backendName() const override
    {
        return "software";
    }
    ImageData scale(const ImageData &src, const RenderSize &target, RenderInterp mode) override;
    ImageData overlayDifference(const ImageData &base, const ImageData &diff,
                                double alpha) const override;
    ImageData scaleRegion(const ImageData &src, const RenderRect &region, const RenderSize &target,
                          RenderInterp mode) override;
    ImageData heatMap(const ImageData &gray, const RenderRect &rect) const override;
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

    static RenderCommand drawImage(const ImageData &img, const RenderSize &tgt, RenderInterp m)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawImage;
        c.srcImage = img;
        c.targetSize = tgt;
        c.interp = static_cast<int>(m);
        return c;
    }
    static RenderCommand drawOverlay(const ImageData &img, double a)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawOverlay;
        c.overlayImage = img;
        c.alpha = a;
        return c;
    }
    // bins is the histogram luminance array (typically 256 entries). Using
    // std::span removes the separate count argument and the manual
    // `i < n && i < 256` bounds hack, eliminating an out-of-bounds hazard.
    static RenderCommand drawHistogram(std::span<const int> bins, const RenderRect &r)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawHistogram;
        const int n = std::min(static_cast<int>(bins.size()), 256);
        c.histCount = n;
        c.rect = r;
        for (int i = 0; i < n; ++i)
            c.histData[i] = bins[i];
        return c;
    }
    static RenderCommand drawHeatmap(const ImageData &gray, const RenderRect &r)
    {
        RenderCommand c;
        c.type = RenderCommandType::DrawHeatmap;
        c.srcImage = gray;
        c.rect = r;
        return c;
    }
    static RenderCommand drawSelection(const RenderRect &r, int rgb)
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
    static RenderEngine &instance();

    void setBackend(std::unique_ptr<Renderer> r);
    // Query the active backend name (useful for tests/logging).
    std::string backendName() const;

    ImageData scale(const ImageData &src, const RenderSize &target,
                    RenderInterp mode = RenderInterp::Bilinear);
    ImageData overlayDifference(const ImageData &base, const ImageData &diff,
                                double alpha = 0.5) const;
    ImageData scaleRegion(const ImageData &src, const RenderRect &region, const RenderSize &target,
                          RenderInterp mode = RenderInterp::Bilinear);
    ImageData heatMap(const ImageData &gray, const RenderRect &rect) const;

    // ─── RenderCommand pipeline ─────────────────────────────────────────────
    // Execute a single command. Self-contained commands (DrawImage, DrawHeatmap,
    // DrawHistogram) produce a fresh ImageData; other commands return null.
    ImageData executeCommand(const RenderCommand &cmd) const;
    // Execute a command against the current buffer, returning the new buffer.
    // Used to composite overlays (DrawOverlay/Selection/Marker/Histogram).
    ImageData executeCommand(const RenderCommand &cmd, const ImageData &buffer) const;
    // Execute a batch sequentially; each command receives the prior output as
    // its buffer. The initial buffer is empty.
    ImageData executeCommands(const std::vector<RenderCommand> &cmds) const;

    static ImageData scaleStatic(const ImageData &src, const RenderSize &target,
                                 RenderInterp mode = RenderInterp::Bilinear);
    static ImageData overlayDifferenceStatic(const ImageData &base, const ImageData &diff,
                                             double alpha = 0.5);
    static ImageData scaleRegionStatic(const ImageData &src, const RenderRect &region,
                                       const RenderSize &target,
                                       RenderInterp mode = RenderInterp::Bilinear);

    // RenderCommand pipeline: execute a single command onto a QPainter.
    // `viewport` is the cell's local rectangle the painter draws into.
    // SmoothPixmapTransform is enabled while drawing when cmd.interp != 0.
    void executeCommand(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);

  private:
    RenderEngine();
    std::unique_ptr<Renderer> m_backend;

    // Command dispatch targets.
    void executeDrawImage(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
    void executeDrawOverlay(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
    void executeDrawSelection(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
    void executeDrawHistogram(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
    void executeDrawHeatmap(QPainter &painter, const RenderCommand &cmd, const QRect &viewport);
};
