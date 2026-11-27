// Review display adjustment (report feature 5): exposure, gamma, auto-stretch
// and a 3D LUT. The panel and its wiring live in DisplayAdjustController; the
// viewer only keeps the public surface (toggle / is-visible) and forwards.
#include "imageviewer.h"
#include "imageviewer_p.h"

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
    if (d->tools.adjust)
        d->tools.adjust->applyCurrent();
    else
        setDisplayColorContext(m_displayColorTarget);
}

bool ImageViewer::isAdjustPanelVisible() const
{
    return d->tools.adjust && d->tools.adjust->isPanelVisible();
}

void ImageViewer::setAdjustPanelVisible(bool visible)
{
    if (!d->tools.adjust)
    {
        d->tools.adjust = std::make_unique<mviewer::ui::DisplayAdjustController>(this);
        d->tools.adjust->setAutoAnchored(false); // layoutFloatingOverlays() owns placement
        d->tools.adjust->attach(
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
    // Guard the requested state: toggle() flips, so asking to show an already
    // visible panel would hide it.
    if (visible == isAdjustPanelVisible())
        return;
    d->tools.adjust->toggle();
    if (visible)
    {
        layoutFloatingOverlays();
        if (auto *panel = d->tools.adjust->panelWidget())
            panel->raise();
    }
}
