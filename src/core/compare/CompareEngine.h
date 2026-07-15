#pragma once

#include "core/compare/BlinkController.h"
#include "core/image/ImageFrame.h"
#include "domain/CompareSession.h"
#include "domain/Selection.h"

#include <algorithm>
#include <string>
#include <vector>

struct CellPoint
{
    int x = 0, y = 0;
};
struct CellSize
{
    int w = 0, h = 0;
};
struct Vec2
{
    double x = 0.0, double y = 0.0;
};

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

// Per-cell view state. Named CellState inside controllers; CellTransform is
// kept as a backward-compatible alias (same shape: scale + Vec2 offset).
struct CellState
{
    double scale = 1.0;
    Vec2 offset;
};
using CellTransform = CellState;

enum class CompareState
{
    Idle,
    Comparing,
    SyncZoom,
    SyncDrag
};

// SyncController owns the shared zoom/pan transform plus the independent
// per-cell transforms (used when sync is disabled).
class SyncController
{
public:
    void setScale(double s);
    void setOffset(double ox, double oy);
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);
    void zoomAtCell(int index, double factor);
    void setEnabled(bool on) { m_sync.enabled = on; }
    bool enabled() const { return m_sync.enabled; }
    void setCellCount(int n) { m_cells.resize(n); }
    void setCellScale(int index, double s);
    void setCellOffset(int index, double ox, double oy);
    void fitCell(int index, const CellSize& viewport, const CellSize& imageSize);
    void reset();

    CellState& cell(int index);
    const CellState& cell(int index) const;

    double scale() const { return m_sync.scale; }
    Vec2 offset() const { return m_sync.offset; }
    const SyncTransform& transform() const { return m_sync; }
    const std::vector<CellState>& cells() const { return m_cells; }

private:
    SyncTransform m_sync;
    std::vector<CellState> m_cells;
};

// SelectionController owns the ROI selection plus sync/enable flags.
class SelectionController
{
public:
    void setSelection(const mviewer::domain::Selection& s) { m_selection = s; }
    void clearSelection() { m_selection = mviewer::domain::Selection{}; }
    void setEnabled(bool on) { m_enabled = on; }
    void setSyncAcrossCells(bool on) { m_synced = on; }

    const mviewer::domain::Selection& selection() const { return m_selection; }
    bool enabled() const { return m_enabled; }
    bool synced() const { return m_synced; }

private:
    mviewer::domain::Selection m_selection;
    bool m_enabled = true;
    bool m_synced = false;
};

// ViewportController owns the viewport size and the grid (cols/rows).
class ViewportController
{
public:
    void setViewport(int w, int h)
    {
        m_vpW = w;
        m_vpH = h;
    }
    void setCellCount(int n)
    {
        const CompareLayout l = CompareLayout::forCount(n);
        m_cols = l.cols;
        m_rows = l.rows;
    }

    CellSize cellSize() const { return CellSize{m_vpW / m_cols, m_vpH / m_rows}; }
    CellPoint cellPos(int index) const
    {
        const CellSize cs = cellSize();
        const int c = index % m_cols;
        const int r = index / m_cols;
        return CellPoint{c * cs.w, r * cs.h};
    }

    int cols() const { return m_cols; }
    int rows() const { return m_rows; }

    double fitScale(CellSize imageSize) const
    {
        if (imageSize.w <= 0 || imageSize.h <= 0)
            return 1.0;
        const CellSize cs = cellSize();
        if (cs.w <= 0 || cs.h <= 0)
            return 1.0;
        return std::min(
            static_cast<double>(cs.w) / imageSize.w, static_cast<double>(cs.h) / imageSize.h);
    }

private:
    int m_vpW = 0, m_vpH = 0;
    int m_cols = 1, m_rows = 1;
};

// CompareEngine: pure facade. Owns images + blink, and composes the three
// independent controllers (sync, selection, viewport).
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
    const SyncTransform& syncTransform() const { return m_sync.transform(); }
    void setSyncEnabled(bool on) { m_sync.setEnabled(on); }
    bool syncEnabled() const { return m_sync.enabled(); }
    void setScale(double s) { m_sync.setScale(s); }
    void setOffset(double ox, double oy) { m_sync.setOffset(ox, oy); }
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1)
    {
        m_sync.zoomAt(viewX, viewY, factor, exceptIndex);
    }

    // Per-cell independent transform (when sync off)
    double cellScale(int index) const { return m_sync.cell(index).scale; }
    Vec2 cellOffset(int index) const { return m_sync.cell(index).offset; }
    void setCellScale(int index, double s) { m_sync.setCellScale(index, s); }
    void setCellOffset(int index, double ox, double oy) { m_sync.setCellOffset(index, ox, oy); }
    const CellTransform& cellTransform(int index) const { return m_sync.cell(index); }
    void fitCell(int index, const CellSize& viewport, const CellSize& imageSize)
    {
        m_sync.fitCell(index, viewport, imageSize);
    }

    // Blink
    int blinkIndex() const { return m_blink.blinkIndex(); }
    void setBlinkIndex(int idx);
    void clearBlink() { m_blink.clearBlink(); }

    // Difference
    ImageData differenceMap(int index, int baseIndex = 0);

    // Access controllers / blink
    const BlinkController& blinkController() const { return m_blink; }
    BlinkController& blinkController() { return m_blink; }
    const SyncController& sync() const { return m_sync; }
    SyncController& sync() { return m_sync; }
    const SelectionController& selection() const { return m_selection; }
    SelectionController& selection() { return m_selection; }
    const ViewportController& viewport() const { return m_viewport; }
    ViewportController& viewport() { return m_viewport; }

private:
    void rebuildLayout();

    std::vector<std::shared_ptr<ImageFrame>> m_images;
    CompareLayout m_layout;
    BlinkController m_blink;
    SyncController m_sync;
    SelectionController m_selection;
    ViewportController m_viewport;
};
