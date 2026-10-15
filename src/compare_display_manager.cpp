#include "compare_display_manager.h"

namespace mviewer::ui
{

CompareDisplayManager::CompareDisplayManager(QObject *parent) : QObject(parent)
{
}

void CompareDisplayManager::setColumns(int cols)
{
    const int c = std::max(1, cols);
    if (m_columns != c)
    {
        m_columns = c;
        emit columnsChanged(m_columns);
    }
}

void CompareDisplayManager::setUniformScale(bool on)
{
    if (m_uniformScale != on)
    {
        m_uniformScale = on;
        emit uniformScaleToggled(m_uniformScale);
        emit fitRequested();
    }
}

void CompareDisplayManager::setSyncZoom(bool on)
{
    if (m_syncZoom != on)
    {
        m_syncZoom = on;
        emit syncZoomToggled(m_syncZoom);
    }
}

void CompareDisplayManager::setSyncDrag(bool on)
{
    if (m_syncDrag != on)
    {
        m_syncDrag = on;
        emit syncDragToggled(m_syncDrag);
    }
}

void CompareDisplayManager::setFitScales(std::vector<double> scales)
{
    m_fitScales = std::move(scales);
}

double CompareDisplayManager::computeFitScale(int viewW, int viewH, int imgW, int imgH) noexcept
{
    if (viewW <= 0 || viewH <= 0 || imgW <= 0 || imgH <= 0)
        return 1.0;
    const double scaleX = static_cast<double>(viewW) / static_cast<double>(imgW);
    const double scaleY = static_cast<double>(viewH) / static_cast<double>(imgH);
    return std::min(scaleX, scaleY);
}

double CompareDisplayManager::computeFitScale(const QSize &viewSize, const QSize &imgSize) noexcept
{
    return computeFitScale(viewSize.width(), viewSize.height(), imgSize.width(), imgSize.height());
}

double CompareDisplayManager::computeUniformFitScale(const std::vector<QSize> &viewSizes,
                                                     const std::vector<QSize> &imgSizes) noexcept
{
    const size_t count = std::min(viewSizes.size(), imgSizes.size());
    if (count == 0)
        return 1.0;

    double minScale = -1.0;
    for (size_t i = 0; i < count; ++i)
    {
        const double s = computeFitScale(viewSizes[i], imgSizes[i]);
        if (minScale < 0.0 || s < minScale)
            minScale = s;
    }
    return (minScale > 0.0) ? minScale : 1.0;
}

std::pair<int, int> CompareDisplayManager::computeGridRowsCols(int imageCount, int targetCols) noexcept
{
    if (imageCount <= 0)
        return {0, 0};
    const int cols = std::clamp(targetCols, 1, imageCount);
    const int rows = (imageCount + cols - 1) / cols;
    return {rows, cols};
}

} // namespace mviewer::ui
