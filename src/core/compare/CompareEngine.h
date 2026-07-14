#pragma once

#include "core/image/ImageFrame.h"
#include "domain/CompareSession.h"

#include <string>
#include <vector>

struct CellPoint { int x = 0, int y = 0; };
struct CellSize  { int w = 0, int h = 0; };
struct Vec2      { double x = 0.0, double y = 0.0; };

// Comparison grid layout rule
struct CompareLayout
{
    int cols = 0, rows = 0, imageCount = 0;
    static CompareLayout forCount(int n);
    CellPoint cellPos(int index, const CellSize& viewport) const;
    CellSize cellSize(const CellSize& viewport) const;
};

struct SyncTransform
{
    double scale = 1.0;
    Vec2 offset;
    bool enabled = true;
};

struct CellTransform
{
    double scale = 1.0;
    Vec2 offset;
};

enum class CompareState { Idle, Comparing, SyncZoom, SyncDrag };

// Blink controller: owns blink state (which image is highlighted)
class BlinkController
{
public:
    explicit BlinkController(int imageCount = 0) : m_imageCount(imageCount) {}
    void setBlinkIndex(int idx);
    void clearBlink();
    int blinkIndex() const { return m_blinkIndex; }
    bool isBlinking() const { return m_blinkIndex >= 0; }
    void setImageCount(int n) { m_imageCount = n; }
private:
    int m_imageCount = 0;
    int m_blinkIndex = -1;
};

class CompareEngine
{
public:
    CompareEngine();

    void setImages(const std::vector<std::string>& paths);
    void addImage(const std::string& path);
    void removeImage(int index);
    void clear();

    mviewer::domain::CompareSession session() const;
    int imageCount() const { return static_cast<int>(m_images.size()); }
    const std::shared_ptr<ImageFrame>& image(int index) const { return m_images[index]; }
    const ImageFrame* imageAt(int index) const;
    const CompareLayout& layout() const { return m_layout; }

    // Sync transform
    const SyncTransform& syncTransform() const { return m_sync; }
    void setSyncEnabled(bool on) { m_sync.enabled = on; }
    bool syncEnabled() const { return m_sync.enabled; }
    void setScale(double s);
    void setOffset(double ox, double oy);
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);

    // Per-cell independent transform (when sync off)
    double cellScale(int index) const;
    Vec2 cellOffset(int index) const;
    void setCellScale(int index, double s);
    void setCellOffset(int index, double ox, double oy);
    const CellTransform& cellTransform(int index) const;
    void fitCell(int index, const CellSize& viewport, const CellSize& imageSize);

    // Blink
    int blinkIndex() const;
    void setBlinkIndex(int idx);
    void clearBlink();

    // Difference
    ImageData differenceMap(int index, int baseIndex = 0);

    // Access blink controller
    const BlinkController& blinkController() const { return m_blink; }
    BlinkController& blinkController() { return m_blink; }

private:
    void rebuildLayout();

    std::vector<std::shared_ptr<ImageFrame>> m_images;
    CompareLayout m_layout;
    SyncTransform m_sync;
    std::vector<CellTransform> m_cells;
    BlinkController m_blink;
};
