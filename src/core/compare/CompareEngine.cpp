#include "core/compare/CompareEngine.h"

#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageRepository.h"

#include <algorithm>
#include <cassert>

// ─── CompareEngine (facade) ─────────────────────────────────────────────────

CompareEngine::CompareEngine()
    : m_layout(CompareLayout::forCount(0))
    , m_blink(imageCount())
{
}

void CompareEngine::setImages(const std::vector<std::string>& paths)
{
    m_images.clear();
    m_images.reserve(paths.size());
    for (const auto& p : paths)
    {
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
    if (index < 0 || index >= static_cast<int>(m_images.size()))
        return;
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
    if (index < 0 || index >= static_cast<int>(m_images.size()))
        return nullptr;
    return m_images[index].get();
}

void CompareEngine::rebuildLayout()
{
    m_layout = CompareLayout::forCount(imageCount());
    m_sync.setCellCount(imageCount());
    m_viewport.setCellCount(imageCount());
    m_blink.setImageCount(imageCount());
}

mviewer::domain::CompareSession CompareEngine::session() const
{
    mviewer::domain::CompareSession s;
    s.imageIds.reserve(imageCount());
    for (int i = 0; i < imageCount(); ++i)
        s.imageIds.push_back(m_images[i]->metadata().filePath);
    const std::vector<CellState>& cells = m_sync.cells();
    s.cells.resize(cells.size());
    for (size_t i = 0; i < cells.size(); ++i)
    {
        s.cells[i].scale = cells[i].scale;
        s.cells[i].offsetX = cells[i].offset.x;
        s.cells[i].offsetY = cells[i].offset.y;
    }
    s.syncMode = m_sync.enabled() ? mviewer::domain::SyncMode::All : mviewer::domain::SyncMode::Off;
    s.blinkIndex = m_blink.blinkIndex();
    s.sharedScale = m_sync.scale();
    s.sharedOffsetX = m_sync.offset().x;
    s.sharedOffsetY = m_sync.offset().y;
    s.cols = m_layout.cols;
    s.rows = m_layout.rows;
    return s;
}

ImageData CompareEngine::differenceMap(int index, int baseIndex)
{
    if (index < 0 || index >= imageCount())
        return ImageData();
    if (baseIndex < 0 || baseIndex >= imageCount())
        return ImageData();
    return DifferenceEngine::differenceMap(
        m_images[baseIndex]->pixels(), m_images[index]->pixels());
}

void CompareEngine::applySelectionToAll(const mviewer::domain::Selection& sel)
{
    m_selection.setSelection(sel);
    for (auto& frame : m_images)
    {
        if (frame)
            frame->setSelection(sel);
    }
}

PixelController::ProbeResult CompareEngine::inspectPixel(int imgX, int imgY, int baseIndex) const
{
    std::vector<ImageData> frames;
    frames.reserve(m_images.size());
    for (const auto& f : m_images)
        frames.push_back(f ? f->pixels() : ImageData{});
    return m_pixel.inspect(frames, imgX, imgY, baseIndex);
}
