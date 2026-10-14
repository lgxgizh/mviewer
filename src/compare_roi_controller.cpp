#include "compare_roi_controller.h"

#include <algorithm>

namespace mviewer::ui
{

CompareRoiController::CompareRoiController(QObject *parent) : QObject(parent)
{
}

void CompareRoiController::setROI(const mviewer::domain::Selection &sel)
{
    if (m_roi.x == sel.x && m_roi.y == sel.y && m_roi.width == sel.width &&
        m_roi.height == sel.height)
    {
        return;
    }
    m_roi = sel;
    bumpGeneration();
    emit roiChanged(m_roi);
}

void CompareRoiController::clearROI()
{
    if (m_roi.isEmpty())
        return;
    m_roi = mviewer::domain::Selection();
    bumpGeneration();
    emit roiCleared();
    emit roiChanged(m_roi);
}

void CompareRoiController::nudgeROI(int dx, int dy, int maxWidth, int maxHeight)
{
    if (m_roi.isEmpty())
        return;

    int newX = m_roi.x + dx;
    int newY = m_roi.y + dy;

    if (maxWidth > 0)
    {
        newX = std::clamp(newX, 0, std::max(0, maxWidth - m_roi.width));
    }
    if (maxHeight > 0)
    {
        newY = std::clamp(newY, 0, std::max(0, maxHeight - m_roi.height));
    }

    if (newX != m_roi.x || newY != m_roi.y)
    {
        m_roi.x = newX;
        m_roi.y = newY;
        bumpGeneration();
        emit roiChanged(m_roi);
    }
}

void CompareRoiController::setMeasurementState(ROIMeasurementState state)
{
    if (m_state == state)
        return;
    m_state = state;
    emit measurementStateChanged(m_state);
}

} // namespace mviewer::ui
