#include "core/compare/CompareEngine.h"

#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"

#include <algorithm>
#include <cassert>

CompareLayout CompareLayout::forCount(int n)
{
    CompareLayout l;
    l.imageCount = n;
    if (n <= 1) {
        l.cols = l.rows = 1;
        return l;
    }
    switch (n) {
    case 2: l.cols = 2; l.rows = 1; break;
    case 3: l.cols = 3; l.rows = 1; break;
    case 4: l.cols = 2; l.rows = 2; break;
    default: l.cols = 4; l.rows = 2; break;
    }
    return l;
}

CellPoint CompareLayout::cellPos(int index, const CellSize& viewport) const
{
    if (imageCount <= 0) return CellPoint{0, 0};
    const int cellW = viewport.w / cols;
    const int cellH = viewport.h / rows;
    const int c = index % cols;
    const int r = index / cols;
    return CellPoint{c * cellW, r * cellH};
}

CellSize CompareLayout::cellSize(const CellSize& viewport) const
{
    if (imageCount <= 0) return viewport;
    return CellSize{viewport.w / cols, viewport.h / rows};
}

CompareEngine::CompareEngine()
    : m_layout(CompareLayout::forCount(0))
    , m_blink(imageCount())
{
}

void CompareEngine::setImages(const std::vector<std::string>& paths)
{
    m_images.clear();
    m_images.reserve(paths.size());
    for (const auto& p : paths) {
        auto r = ImageRepository::instance().load(p);
        if (r.success())
            m_images.push_back(std::move(r.frame));
    }
    rebuildLayout();
    m_blink.clearBlink();
}

void CompareEngine::addImage(const std::string& path)
{
    auto r = ImageRepository::instance().load(path);
    if (r.success())
        m_images.push_back(std::move(r.frame));
    rebuildLayout();
}

void CompareEngine::removeImage(int index)
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return;
    m_images.erase(m_images.begin() + index);
    rebuildLayout();
}

void CompareEngine::clear()
{
    m_images.clear();
    rebuildLayout();
    m_blink.clearBlink();
}

const ImageFrame* CompareEngine::imageAt(int index) const
{
    if (index < 0 || index >= static_cast<int>(m_images.size())) return nullptr;
    return m_images[index].get();
}

void CompareEngine::rebuildLayout()
{
    m_layout = CompareLayout::forCount(imageCount());
    m_cells.resize(imageCount());
    m_blink.setImageCount(imageCount());
}

mviewer::domain::CompareSession CompareEngine::session() const
{
    mviewer::domain::CompareSession s;
    s.imageIds.reserve(imageCount());
    for (int i = 0; i < imageCount(); ++i)
        s.imageIds.push_back(m_images[i]->metadata().filePath);
    s.cells.resize(m_cells.size());
    for (size_t i = 0; i < m_cells.size(); ++i) {
        s.cells[i].scale   = m_cells[i].scale;
        s.cells[i].offsetX = m_cells[i].offset.x;
        s.cells[i].offsetY = m_cells[i].offset.y;
    }
    s.syncMode      = m_sync.enabled ? mviewer::domain::SyncMode::All : mviewer::domain::SyncMode::Off;
    s.blinkIndex    = m_blink.blinkIndex();
    s.sharedScale   = m_sync.scale;
    s.sharedOffsetX = m_sync.offset.x;
    s.sharedOffsetY = m_sync.offset.y;
    s.cols          = m_layout.cols;
    s.rows          = m_layout.rows;
    return s;
}

void CompareEngine::setScale(double s)
{
    m_sync.scale = s;
    if (m_sync.enabled)
        for (auto& c : m_cells) c.scale = s;
}

void CompareEngine::setOffset(double ox, double oy)
{
    m_sync.offset.x = ox;
    m_sync.offset.y = oy;
    if (m_sync.enabled)
        for (auto& c : m_cells) { c.offset.x = ox; c.offset.y = oy; }
}

void CompareEngine::zoomAt(double /*viewX*/, double /*viewY*/, double factor, int exceptIndex)
{
    if (m_sync.enabled) {
        const double ns = m_sync.scale * factor;
        for (int i = 0; i < static_cast<int>(m_cells.size()); ++i)
            if (i != exceptIndex) m_cells[i].scale = ns;
        m_sync.scale = ns;
    } else if (exceptIndex >= 0 && exceptIndex < static_cast<int>(m_cells.size())) {
        m_cells[exceptIndex].scale *= factor;
    }
}

void CompareEngine::setCellScale(int index, double s)
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        m_cells[index].scale = s;
}

void CompareEngine::setCellOffset(int index, double ox, double oy)
{
    if (0 <= index && index < static_cast<int>(m_cells.size())) {
        m_cells[index].offset.x = ox;
        m_cells[index].offset.y = oy;
    }
}

double CompareEngine::cellScale(int index) const
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        return m_cells[index].scale;
    return 1.0;
}

Vec2 CompareEngine::cellOffset(int index) const
{
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        return m_cells[index].offset;
    return {0.0, 0.0};
}

void CompareEngine::fitCell(int index, const CellSize& viewport, const CellSize& imageSize)
{
    if (index < 0 || index >= static_cast<int>(m_cells.size())) return;
    if (imageSize.w <= 0 || imageSize.h <= 0) return;
    const double scaleX = static_cast<double>(viewport.w) / imageSize.w;
    const double scaleY = static_cast<double>(viewport.h) / imageSize.h;
    m_cells[index].scale = std::min(scaleX, scaleY);
    m_cells[index].offset.x = 0;
    m_cells[index].offset.y = 0;
}

const CellTransform& CompareEngine::cellTransform(int index) const
{
    static const CellTransform kDefault{};
    if (0 <= index && index < static_cast<int>(m_cells.size()))
        return m_cells[index];
    return kDefault;
}

int CompareEngine::blinkIndex() const
{
    return m_blink.blinkIndex();
}

void CompareEngine::setBlinkIndex(int idx)
{
    if (idx < -1 || idx >= imageCount()) return;
    m_blink.setBlinkIndex(idx);
}

void CompareEngine::clearBlink()
{
    m_blink.clearBlink();
}

ImageData CompareEngine::differenceMap(int index, int baseIndex)
{
    if (index < 0 || index >= imageCount()) return ImageData();
    if (baseIndex < 0 || baseIndex >= imageCount()) return ImageData();
    return DifferenceEngine::differenceMap(m_images[baseIndex]->pixels(), m_images[index]->pixels());
}
