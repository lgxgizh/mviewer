#include "core/compare/CompareEngine.h"

#include <algorithm>
#include <cmath>

namespace
{

thread_local CellState s_fallbackCell{};
const CellState kDefaultCell{};

bool finitePositiveZoom(double viewX, double viewY, double factor)
{
    return std::isfinite(viewX) && std::isfinite(viewY) && std::isfinite(factor) && factor > 0.0;
}

double anchoredOffset(double anchor, double oldOffset, double factor)
{
    return anchor - (anchor - oldOffset) * factor;
}

void zoomCellAbout(CellState &cell, double viewX, double viewY, double factor)
{
    cell.offset.x = anchoredOffset(viewX, cell.offset.x, factor);
    cell.offset.y = anchoredOffset(viewY, cell.offset.y, factor);
    cell.scale *= factor;
}

} // namespace

void SyncController::setScale(double s)
{
    m_sync.scale = s;
    if (m_sync.zoomEnabled)
        for (auto &c : m_cells)
            c.scale = s;
}

void SyncController::setOffset(double ox, double oy)
{
    m_sync.offset = Vec2{ox, oy};
    if (m_sync.dragEnabled)
        for (auto &c : m_cells)
        {
            c.offset.x = ox;
            c.offset.y = oy;
        }
}

void SyncController::zoomAt(double viewX, double viewY, double factor, int exceptIndex)
{
    // exceptIndex is the pane the caller already updated when sync is on, and
    // the only pane to update when both sync axes are off. A non-finite factor
    // must not poison every cell scale.
    if (!finitePositiveZoom(viewX, viewY, factor))
        return;

    const int count = static_cast<int>(m_cells.size());
    const bool zoom = m_sync.zoomEnabled;
    const bool drag = m_sync.dragEnabled;
    if (!zoom && !drag)
    {
        if (exceptIndex >= 0 && exceptIndex < count)
            zoomCellAbout(m_cells[static_cast<size_t>(exceptIndex)], viewX, viewY, factor);
        return;
    }

    if (zoom && drag)
    {
        const double nextScale = m_sync.scale * factor;
        const Vec2 nextOffset{anchoredOffset(viewX, m_sync.offset.x, factor),
                              anchoredOffset(viewY, m_sync.offset.y, factor)};
        for (int i = 0; i < count; ++i)
        {
            if (i == exceptIndex)
                continue;
            m_cells[static_cast<size_t>(i)].scale = nextScale;
            m_cells[static_cast<size_t>(i)].offset = nextOffset;
        }
        m_sync.scale = nextScale;
        m_sync.offset = nextOffset;
        return;
    }

    if (zoom)
        zoomIndependentPanes(viewX, viewY, factor, exceptIndex);
    else
        zoomDragSyncedPane(viewX, viewY, factor, exceptIndex);
}

void SyncController::zoomIndependentPanes(double viewX, double viewY, double factor,
                                          int exceptIndex)
{
    // Zoom sync without drag sync: each pane keeps its own pan and stays
    // anchored under the cursor. Collapsing every pane onto one scale would
    // throw away per-image fit.
    const int count = static_cast<int>(m_cells.size());
    int sample = -1;
    for (int i = 0; i < count; ++i)
    {
        if (i == exceptIndex)
            continue;
        zoomCellAbout(m_cells[static_cast<size_t>(i)], viewX, viewY, factor);
        if (sample < 0)
            sample = i;
    }
    m_sync.scale *= factor;
    if (sample >= 0)
        m_sync.offset = m_cells[static_cast<size_t>(sample)].offset;
}

void SyncController::zoomDragSyncedPane(double viewX, double viewY, double factor, int exceptIndex)
{
    // Drag sync without zoom sync: only the reference pane changes scale.
    // Every pane then shares the pan that keeps that zoom anchored.
    const int count = static_cast<int>(m_cells.size());
    if (exceptIndex < 0 || exceptIndex >= count)
        return;
    m_cells[static_cast<size_t>(exceptIndex)].scale *= factor;
    const Vec2 nextOffset{anchoredOffset(viewX, m_sync.offset.x, factor),
                          anchoredOffset(viewY, m_sync.offset.y, factor)};
    m_sync.offset = nextOffset;
    for (auto &cell : m_cells)
        cell.offset = nextOffset;
}

void SyncController::zoomAtCell(int index, double factor)
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        m_cells[index].scale *= factor;
}

void SyncController::setCellScale(int index, double s)
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        m_cells[index].scale = s;
}

void SyncController::setCellOffset(int index, double ox, double oy)
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
    {
        m_cells[index].offset.x = ox;
        m_cells[index].offset.y = oy;
    }
}

void SyncController::swapCells(int a, int b)
{
    const int count = static_cast<int>(m_cells.size());
    if (a >= 0 && a < count && b >= 0 && b < count && a != b)
    {
        std::swap(m_cells[a], m_cells[b]);
    }
}

void SyncController::fitCell(int index, const CellSize &viewport, const CellSize &imageSize)
{
    if (index < 0 || index >= static_cast<int>(m_cells.size()))
        return;
    if (imageSize.w <= 0 || imageSize.h <= 0)
        return;
    const double scaleX = static_cast<double>(viewport.w) / imageSize.w;
    const double scaleY = static_cast<double>(viewport.h) / imageSize.h;
    m_cells[index].scale = std::min(scaleX, scaleY);
    m_cells[index].offset.x = 0;
    m_cells[index].offset.y = 0;
}

void SyncController::reset()
{
    m_sync = SyncTransform{};
    for (auto &c : m_cells)
        c = CellState{};
}

CellState &SyncController::cell(int index)
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        return m_cells[index];
    s_fallbackCell = CellState{};
    return s_fallbackCell;
}

const CellState &SyncController::cell(int index) const
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        return m_cells[index];
    return kDefaultCell;
}
