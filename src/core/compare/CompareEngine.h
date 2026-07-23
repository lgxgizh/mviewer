#pragma once

#include "core/compare/BlinkController.h"
#include "core/compare/CompareTypes.h"
#include "core/compare/PixelController.h"
#include "core/image/ImageFrame.h"
#include "domain/CompareSession.h"
#include "domain/Selection.h"

#include <algorithm>
#include <mutex>
#include <string>
#include <vector>

// SyncController owns the shared zoom/pan transform plus the independent
// per-cell transforms (used when sync is disabled).
class SyncController
{
  public:
    void setScale(double s);
    void setOffset(double ox, double oy);
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1);
    void zoomAtCell(int index, double factor);
    void setEnabled(bool on)
    {
        m_sync.enabled = on;
    }
    bool enabled() const
    {
        return m_sync.enabled;
    }
    void setCellCount(int n)
    {
        m_cells.resize(n);
    }
    void setCellScale(int index, double s);
    void setCellOffset(int index, double ox, double oy);
    void fitCell(int index, const CellSize &viewport, const CellSize &imageSize);
    void reset();

    CellState &cell(int index);
    const CellState &cell(int index) const;

    double scale() const
    {
        return m_sync.scale;
    }
    Vec2 offset() const
    {
        return m_sync.offset;
    }
    const SyncTransform &transform() const
    {
        return m_sync;
    }
    const std::vector<CellState> &cells() const
    {
        return m_cells;
    }

  private:
    SyncTransform m_sync;
    std::vector<CellState> m_cells;
};

// SelectionController owns the ROI selection plus sync/enable flags.
class SelectionController
{
  public:
    void setSelection(const mviewer::domain::Selection &s)
    {
        m_selection = s;
    }
    void clearSelection()
    {
        m_selection = mviewer::domain::Selection{};
    }
    void setEnabled(bool on)
    {
        m_enabled = on;
    }
    void setSyncAcrossCells(bool on)
    {
        m_synced = on;
    }

    const mviewer::domain::Selection &selection() const
    {
        return m_selection;
    }
    bool enabled() const
    {
        return m_enabled;
    }
    bool synced() const
    {
        return m_synced;
    }

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

    CellSize cellSize() const
    {
        return CellSize{m_vpW / m_cols, m_vpH / m_rows};
    }
    CellPoint cellPos(int index) const
    {
        const CellSize cs = cellSize();
        const int c = index % m_cols;
        const int r = index / m_cols;
        return CellPoint{c * cs.w, r * cs.h};
    }

    int cols() const
    {
        return m_cols;
    }
    int rows() const
    {
        return m_rows;
    }
    // P0 #③: set the grid dimensions directly (used for forced layouts).
    void setGrid(int cols, int rows)
    {
        m_cols = cols;
        m_rows = rows;
    }

    double fitScale(CellSize imageSize) const
    {
        if (imageSize.w <= 0 || imageSize.h <= 0)
            return 1.0;
        const CellSize cs = cellSize();
        if (cs.w <= 0 || cs.h <= 0)
            return 1.0;
        return std::min(static_cast<double>(cs.w) / imageSize.w,
                        static_cast<double>(cs.h) / imageSize.h);
    }

  private:
    int m_vpW = 0, m_vpH = 0;
    int m_cols = 1, m_rows = 1;
};

// Result of an asynchronous difference computation. Published on the EventBus
// (event "CompareEngine.DiffResult", scope Application) when requestDiff
// completes. The pixels live in CompareEngine::lastDiff() (mutex-guarded), not
// in this struct, so subscribers read them from the engine after the event.
struct DiffResult
{
    int index = -1;
    int baseIndex = -1;
    bool valid = false;
};

// CompareEngine: pure facade. Owns images + blink, and composes the three
// independent controllers (sync, selection, viewport).
class CompareEngine
{
  public:
    CompareEngine();
    CompareEngine(const CompareEngine &) = delete;
    CompareEngine &operator=(const CompareEngine &) = delete;

    void setImages(const std::vector<std::string> &paths);
    void addImage(const std::string &path);
    void removeImage(int index);
    void clear();
    void swapFrames(int a, int b);

    mviewer::domain::CompareSession session() const;
    int imageCount() const
    {
        return static_cast<int>(m_images.size());
    }
    const std::shared_ptr<ImageFrame> &image(int index) const
    {
        return m_images[index];
    }
    const ImageFrame *imageAt(int index) const;
    const CompareLayout &layout() const
    {
        return m_layout;
    }

    // P0 #③: force a column count for the compare grid (0 = auto by image count).
    void setColumns(int cols);
    int forcedColumns() const
    {
        return m_forcedCols;
    }

    // Sync transform
    const SyncTransform &syncTransform() const
    {
        return m_sync.transform();
    }
    void setSyncEnabled(bool on)
    {
        m_sync.setEnabled(on);
    }
    bool syncEnabled() const
    {
        return m_sync.enabled();
    }
    void setScale(double s)
    {
        m_sync.setScale(s);
    }
    void setOffset(double ox, double oy)
    {
        m_sync.setOffset(ox, oy);
    }
    void zoomAt(double viewX, double viewY, double factor, int exceptIndex = -1)
    {
        m_sync.zoomAt(viewX, viewY, factor, exceptIndex);
    }

    // Per-cell independent transform (when sync off)
    double cellScale(int index) const
    {
        return m_sync.cell(index).scale;
    }
    Vec2 cellOffset(int index) const
    {
        return m_sync.cell(index).offset;
    }
    void setCellScale(int index, double s)
    {
        m_sync.setCellScale(index, s);
    }
    void setCellOffset(int index, double ox, double oy)
    {
        m_sync.setCellOffset(index, ox, oy);
    }
    const CellTransform &cellTransform(int index) const
    {
        return m_sync.cell(index);
    }
    void fitCell(int index, const CellSize &viewport, const CellSize &imageSize)
    {
        m_sync.fitCell(index, viewport, imageSize);
    }

    // Blink
    int blinkIndex() const
    {
        return m_blink.blinkIndex();
    }
    void setBlinkIndex(int idx)
    {
        if (idx < -1 || idx >= imageCount())
            return;
        m_blink.setBlinkIndex(idx);
    }
    void clearBlink()
    {
        m_blink.clearBlink();
    }

    // Difference (synchronous; for tests / callers needing an immediate result)
    ImageData differenceMap(int index, int baseIndex = 0);

    // Difference (asynchronous). Submits the compute to JobSystem (Analysis
    // pool) so the calling thread never blocks; on completion the result is
    // stored in lastDiff() and a "CompareEngine.DiffResult" event is published
    // on the EventBus (scope Application). Returns true if the job was accepted.
    // The UI subscribes to that event and hops to the UI thread before painting.
    bool requestDiff(int index, int baseIndex = 0);

    // Most recent asynchronous diff result (mutex-guarded). valid==false until
    // the first requestDiff completes.
    DiffResult lastDiff() const;
    // By-value: the internal buffer is mutex-guarded and released after the
    // call returns, so returning a reference would dangle. ImageData is
    // shared_ptr-backed, so a value copy is cheap and thread-safe.
    ImageData lastDiffImage() const;

    // Access controllers / blink
    const BlinkController &blinkController() const
    {
        return m_blink;
    }
    BlinkController &blinkController()
    {
        return m_blink;
    }
    const SyncController &sync() const
    {
        return m_sync;
    }
    SyncController &sync()
    {
        return m_sync;
    }
    const SelectionController &selection() const
    {
        return m_selection;
    }
    SelectionController &selection()
    {
        return m_selection;
    }
    // Mirror the synchronized ROI to every owned frame (called by CompareWorkspace).
    void applySelectionToAll(const mviewer::domain::Selection &sel);
    const ViewportController &viewport() const
    {
        return m_viewport;
    }
    ViewportController &viewport()
    {
        return m_viewport;
    }

    // Pixel probe (fifth Compare Engine module). Reads the pixel at (imgX,imgY)
    // from every compared cell and returns samples + deltas vs baseIndex.
    PixelController::ProbeResult inspectPixel(int imgX, int imgY, int baseIndex = 0) const;
    const PixelController &pixel() const
    {
        return m_pixel;
    }
    PixelController &pixel()
    {
        return m_pixel;
    }

  private:
    void rebuildLayout();

    std::vector<std::shared_ptr<ImageFrame>> m_images;
    CompareLayout m_layout;
    int m_forcedCols = 0; // P0 #③: 0 = auto layout by image count
    BlinkController m_blink;
    SyncController m_sync;
    SelectionController m_selection;
    ViewportController m_viewport;
    PixelController m_pixel;

    // Async diff result storage (mutex-guarded; written on worker thread, read
    // on UI/EventBus subscriber thread).
    mutable std::mutex m_diffMtx;
    DiffResult m_lastDiff;
    ImageData m_lastDiffImage;
};
