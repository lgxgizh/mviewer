// Review display adjustment (report feature 5): exposure, gamma, auto-stretch
// and a 3D LUT. The panel and its wiring live in DisplayAdjustController; the
// viewer only keeps the public surface (toggle / is-visible) and forwards.
#include "imageviewer.h"

#include "core/image/DisplayAdjust.h"
#include "core/image/DisplayColorContext.h"
#include "widgets/displayadjustcontroller.h"
#include "widgets/displayadjustpanel.h"

#include <QFileDialog>
#include <QFileInfo>
#include <QMenu>
#include <QPushButton>

mviewer::core::DisplayAdjust &ImageViewer::displayAdjust()
{
    return m_displayColorTarget.adjust;
}

void ImageViewer::addAdjustContextAction(QMenu &menu, QAction *&adjust)
{
    adjust = menu.addAction(tr("Display Adjust (G)"));
    adjust->setCheckable(true);
    adjust->setChecked(isAdjustPanelVisible());
    adjust->setEnabled(hasDisplayImage());
}

bool ImageViewer::handleAdjustContextAction(QAction *chosen, QAction *adjust)
{
    if (chosen == adjust)
    {
        setAdjustPanelVisible(!isAdjustPanelVisible());
        return true;
    }
    return false;
}

void ImageViewer::applyDisplayAdjust()
{
    // The panel mutated the live target; re-publish it so the cache-key change is
    // detected (see ImageViewer::setDisplayColorContext).
    if (m_adjust)
        m_adjust->applyCurrent();
    else
        setDisplayColorContext(m_displayColorTarget);
}

bool ImageViewer::isAdjustPanelVisible() const
{
    return m_adjust && m_adjust->isPanelVisible();
}

void ImageViewer::setAdjustPanelVisible(bool visible)
{
    if (!m_adjust)
    {
        m_adjust = std::make_unique<mviewer::ui::DisplayAdjustController>(this);
        m_adjust->setAutoAnchored(false); // layoutFloatingOverlays() owns placement
        m_adjust->attach(
            this,
            [this]() { return m_displayColorTarget.adjust; },
            [this](const mviewer::core::DisplayAdjust &adjust)
            {
                // Snapshot first: m_displayColorTarget is mutated by the panel's
                // setExposure path above, and comparing it against itself would
                // drop the change entirely.
                auto next = m_displayColorTarget;
                next.adjust = adjust;
                setDisplayColorContext(next);
            });
    }
    if (visible)
    {
        m_adjust->toggle();
        if (isAdjustPanelVisible())
        {
            layoutFloatingOverlays();
            if (auto *panel = m_adjust->panelWidget())
                panel->raise();
        }
    }
    else if (m_adjust->isPanelVisible())
    {
        m_adjust->toggle();
    }
}
