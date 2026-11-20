// Review display adjustment (report feature 5): exposure, gamma, auto-stretch
// and a 3D LUT. The panel mutates a DisplayAdjust; pushing it into the display
// colour context changes the cache key, so the raster and every tile are
// re-materialized through the normal display path.
#include "imageviewer.h"

#include "core/image/DisplayAdjust.h"
#include "core/image/DisplayColorContext.h"
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
    setDisplayColorContext(m_displayColorTarget);
}

void ImageViewer::syncAdjustPanel()
{
    if (!m_adjustPanel)
        return;
    const mviewer::core::DisplayAdjust &adjust = m_displayColorTarget.adjust;
    m_adjustPanel->setExposure(adjust.exposureEv);
    m_adjustPanel->setGamma(adjust.gamma);
    m_adjustPanel->setAutoStretch(adjust.autoStretch);
    m_adjustPanel->setLutLabel(adjust.lutSize > 0 ? tr("3D LUT: %1³").arg(adjust.lutSize)
                                                  : tr("Load 3D LUT…"));
}

void ImageViewer::setAdjustPanelVisible(bool visible)
{
    if (visible && !m_adjustPanel)
    {
        m_adjustPanel = new mviewer::ui::DisplayAdjustPanel(this);
        connect(m_adjustPanel, &mviewer::ui::DisplayAdjustPanel::exposureChanged, this,
                [this](double ev)
                {
                    m_displayColorTarget.adjust.exposureEv = ev;
                    applyDisplayAdjust();
                });
        connect(m_adjustPanel, &mviewer::ui::DisplayAdjustPanel::gammaChanged, this,
                [this](double g)
                {
                    m_displayColorTarget.adjust.gamma = g;
                    applyDisplayAdjust();
                });
        connect(m_adjustPanel, &mviewer::ui::DisplayAdjustPanel::autoStretchToggled, this,
                [this](bool on)
                {
                    m_displayColorTarget.adjust.autoStretch = on;
                    applyDisplayAdjust();
                });
        connect(m_adjustPanel, &mviewer::ui::DisplayAdjustPanel::loadLutRequested, this,
                [this]()
                {
                    const QString path = QFileDialog::getOpenFileName(
                        this, tr("Load 3D LUT"), QString(), tr("Cube LUT (*.cube);;All files (*)"));
                    if (path.isEmpty())
                        return;
                    std::string error;
                    mviewer::core::DisplayAdjust candidate = m_displayColorTarget.adjust;
                    if (!mviewer::core::DisplayAdjust::loadCube(path.toUtf8().toStdString(),
                                                                candidate, error))
                    {
                        emit statusMessageRequested(tr("Could not load the 3D LUT") +
                                                        QStringLiteral(": ") +
                                                        QString::fromStdString(error),
                                                    5000);
                        return;
                    }
                    m_displayColorTarget.adjust = candidate;
                    syncAdjustPanel();
                    applyDisplayAdjust();
                });
        connect(m_adjustPanel, &mviewer::ui::DisplayAdjustPanel::resetRequested, this,
                [this]()
                {
                    m_displayColorTarget.adjust = mviewer::core::DisplayAdjust{};
                    syncAdjustPanel();
                    applyDisplayAdjust();
                });
    }
    if (m_adjustPanel)
    {
        m_adjustPanel->setVisible(visible);
        if (visible)
        {
            syncAdjustPanel();
            const int w = 280;
            const int h = 150;
            m_adjustPanel->setGeometry(width() - w - 12, height() - h - 12, w, h);
            m_adjustPanel->raise();
        }
    }
}

bool ImageViewer::isAdjustPanelVisible() const
{
    return m_adjustPanel && m_adjustPanel->isVisible();
}
