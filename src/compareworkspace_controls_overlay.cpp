// CompareWorkspace display-overlay and pixel-grid control state, split out of
// compareworkspace_controls.cpp to stay under the line budget.
#include "compareworkspace_p.h"

#include <QSignalBlocker>

void CompareWorkspace::setOverlayMode(mviewer::OverlayMode mode)
{
    if (m_displayOverlay == mode)
    {
        if (m_channelCombo)
        {
            const int index = m_channelCombo->findData(static_cast<int>(mode));
            if (index >= 0 && m_channelCombo->currentIndex() != index)
            {
                const QSignalBlocker blocker(m_channelCombo);
                m_channelCombo->setCurrentIndex(index);
            }
        }
        return;
    }
    m_displayOverlay = mode;
    if (m_channelCombo)
    {
        const int index = m_channelCombo->findData(static_cast<int>(mode));
        if (index >= 0)
        {
            const QSignalBlocker blocker(m_channelCombo);
            m_channelCombo->setCurrentIndex(index);
        }
    }
    applyDisplayOverlayToViews();
    m_canvasBaseKey.clear();
    update();
}

void CompareWorkspace::applyDisplayOverlayToViews()
{
    for (RawImageView *view : m_cellViews)
    {
        if (view)
            view->setDisplayOverlay(m_displayOverlay);
    }
}

bool CompareWorkspace::isPixelGridEnabled() const
{
    return m_pixelGridEnabled;
}

void CompareWorkspace::setPixelGridEnabled(bool on)
{
    if (m_pixelGridEnabled == on)
        return;
    m_pixelGridEnabled = on;
    if (m_pixelGridChk && m_pixelGridChk->isChecked() != on)
    {
        const QSignalBlocker b(m_pixelGridChk);
        m_pixelGridChk->setChecked(on);
    }
    for (RawImageView *view : m_cellViews)
    {
        if (view)
            view->setPixelGridEnabled(on);
    }
    if (m_compareCanvas)
        m_compareCanvas->update();
    update();
}
