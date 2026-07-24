#include "core/compare/CompareEngine.h"

#include <algorithm>

namespace
{

CellState kDefaultCell{};

} // namespace

void SyncController::setScale(double s)
{
    m_sync.scale = s;
    if (m_sync.enabled)
        for (auto &c : m_cells)
            c.scale = s;
}

void SyncController::setOffset(double ox, double oy)
{
    m_sync.offset = Vec2{ox, oy};
    if (m_sync.enabled)
        for (auto &c : m_cells)
        {
            c.offset.x = ox;
            c.offset.y = oy;
        }
}

void SyncController::zoomAt(double viewX, double viewY, double factor, int exceptIndex)
{
    if (m_sync.enabled)
    {
        const double ns = m_sync.scale * factor;
        const Vec2 newOffset{
            viewX - (viewX - m_sync.offset.x) * factor,
            viewY - (viewY - m_sync.offset.y) * factor};
        for (int i = 0; i < static_cast<int>(m_cells.size()); ++i)
        {
            if (i == exceptIndex)
                continue;
            m_cells[i].scale = ns;
            m_cells[i].offset = newOffset;
        }
        m_sync.scale = ns;
        m_sync.offset = newOffset;
    }
    else if (exceptIndex >= 0 && exceptIndex < static_cast<int>(m_cells.size()))
    {
        CellState &c = m_cells[exceptIndex];
        const double ns = c.scale * factor;
        c.offset.x = viewX - (viewX - c.offset.x) * factor;
        c.offset.y = viewY - (viewY - c.offset.y) * factor;
        c.scale = ns;
    }
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
    return kDefaultCell;
}

const CellState &SyncController::cell(int index) const
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        return m_cells[index];
    return kDefaultCell;
}
