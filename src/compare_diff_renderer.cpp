#include "compare_diff_renderer.h"

#include "core/compare/DifferenceEngine.h"

#include <QPainter>
#include <algorithm>
#include <cmath>

namespace mviewer::ui
{

CompareDiffRenderer::CompareDiffRenderer(QObject *parent) : QObject(parent)
{
}

void CompareDiffRenderer::setThreshold(uint8_t th)
{
    if (m_threshold != th)
    {
        m_threshold = th;
        emit thresholdChanged(th);
    }
}

void CompareDiffRenderer::setSplitRatio(double ratio)
{
    const double clamped = std::clamp(ratio, 0.0, 1.0);
    if (std::abs(m_splitRatio - clamped) > 1e-4)
    {
        m_splitRatio = clamped;
        emit splitRatioChanged(clamped);
    }
}

void CompareDiffRenderer::setSplitOrientation(Qt::Orientation orient)
{
    m_splitOrientation = orient;
}

void CompareDiffRenderer::setBlendMode(DiffBlendMode mode)
{
    if (m_blendMode != mode)
    {
        m_blendMode = mode;
        emit blendModeChanged(mode);
    }
}

void CompareDiffRenderer::setBlinkActive(bool active)
{
    if (m_blinkActive != active)
    {
        m_blinkActive = active;
        emit blinkStateChanged(active);
    }
}

void CompareDiffRenderer::setBlinkIntervalMs(int ms)
{
    m_blinkIntervalMs = std::clamp(ms, 50, 5000);
}

QImage CompareDiffRenderer::renderSplit(const QImage &imgA, const QImage &imgB, double ratio,
                                        Qt::Orientation orientation)
{
    if (imgA.isNull())
        return imgB;
    if (imgB.isNull())
        return imgA;

    const int w = std::min(imgA.width(), imgB.width());
    const int h = std::min(imgA.height(), imgB.height());
    if (w <= 0 || h <= 0)
        return QImage();

    QImage result(w, h, QImage::Format_ARGB32_Premultiplied);
    result.fill(Qt::transparent);

    QPainter painter(&result);
    painter.setRenderHint(QPainter::Antialiasing, false);

    const double clampedRatio = std::clamp(ratio, 0.0, 1.0);

    if (orientation == Qt::Horizontal)
    {
        const int splitX = static_cast<int>(std::round(w * clampedRatio));
        if (splitX > 0)
        {
            painter.drawImage(QRect(0, 0, splitX, h), imgA, QRect(0, 0, splitX, h));
        }
        if (splitX < w)
        {
            painter.drawImage(QRect(splitX, 0, w - splitX, h), imgB,
                              QRect(splitX, 0, w - splitX, h));
        }
    }
    else
    {
        const int splitY = static_cast<int>(std::round(h * clampedRatio));
        if (splitY > 0)
        {
            painter.drawImage(QRect(0, 0, w, splitY), imgA, QRect(0, 0, w, splitY));
        }
        if (splitY < h)
        {
            painter.drawImage(QRect(0, splitY, w, h - splitY), imgB,
                              QRect(0, splitY, w, h - splitY));
        }
    }

    painter.end();
    return result;
}

ImageData CompareDiffRenderer::computeDifference(const ImageData &imgA, const ImageData &imgB,
                                                 uint8_t threshold)
{
    return DifferenceEngine::differenceMap(imgA, imgB, threshold);
}

} // namespace mviewer::ui
