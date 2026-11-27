#include "widgets/displayadjustcontroller.h"

#include "core/image/DisplayAdjust.h"
#include "widgets/displayadjustpanel.h"

#include <QAction>
#include <QCoreApplication>
#include <QEvent>
#include <QFileDialog>
#include <QMessageBox>
#include <QPointer>
#include <QSignalBlocker>

namespace mviewer::ui
{
namespace
{
// Pins the panel to the host's top-right corner: both hosts are plain QWidgets
// (a fullscreen dialog / the viewer), so no layout manager does it.
class AdjustAnchor final : public QObject
{
  public:
    AdjustAnchor(QWidget *panel, QObject *host) : QObject(host), m_panel(panel) {}

    bool eventFilter(QObject *watched, QEvent *event) override
    {
        if (watched == parent() && event->type() == QEvent::Resize)
            place();
        return false;
    }

    void place()
    {
        const auto *host = qobject_cast<QWidget *>(parent());
        if (host != nullptr && m_panel)
            m_panel->move(host->width() - m_panel->width() - 12, 12);
    }

  private:
    QPointer<QWidget> m_panel;
};
} // namespace

class DisplayAdjustController::Impl
{
  public:
    QPointer<QWidget> host;
    CurrentFn current;
    ApplyFn apply;
    QPointer<QAction> action;
    QPointer<mviewer::ui::DisplayAdjustPanel> panel;
    AdjustAnchor *anchor = nullptr;
    bool autoAnchored = true;

    void publish(mviewer::core::DisplayAdjust adjust) const
    {
        if (apply)
            apply(adjust);
    }
};

DisplayAdjustController::DisplayAdjustController(QObject *parent)
    : QObject(parent), m_impl(new Impl())
{
}

DisplayAdjustController::~DisplayAdjustController()
{
    delete m_impl;
}

void DisplayAdjustController::attach(QWidget *host, CurrentFn current, ApplyFn apply)
{
    // Drop the previous session's panel/anchor first: leaving them behind would
    // keep a hidden panel alive, its event filter installed on the old host and
    // the action checked after a detach.
    if (m_impl->anchor)
    {
        if (m_impl->host)
            m_impl->host->removeEventFilter(m_impl->anchor);
        m_impl->anchor->deleteLater();
        m_impl->anchor = nullptr;
    }
    if (m_impl->panel)
    {
        m_impl->panel->hide();
        m_impl->panel->deleteLater();
        m_impl->panel = nullptr;
    }
    if (m_impl->action)
    {
        m_impl->action->setChecked(false);
        m_impl->action->setEnabled(host != nullptr);
    }
    m_impl->host = host;
    m_impl->current = std::move(current);
    m_impl->apply = std::move(apply);
}

void DisplayAdjustController::detach()
{
    attach(nullptr, nullptr, nullptr);
}

void DisplayAdjustController::setAction(QAction *action)
{
    m_impl->action = action;
    if (action)
        action->setEnabled(m_impl->host != nullptr);
}

bool DisplayAdjustController::isPanelVisible() const
{
    return m_impl->panel && m_impl->panel->isVisible();
}

QWidget *DisplayAdjustController::panelWidget() const
{
    return m_impl->panel;
}

void DisplayAdjustController::setAutoAnchored(bool anchored)
{
    m_impl->autoAnchored = anchored;
}

void DisplayAdjustController::applyCurrent()
{
    if (m_impl->current)
        m_impl->publish(m_impl->current());
}

void DisplayAdjustController::toggle()
{
    if (!m_impl->host)
    {
        if (m_impl->action)
            m_impl->action->setChecked(false);
        return;
    }
    const bool want = m_impl->action ? m_impl->action->isChecked() : !isPanelVisible();

    if (want && !m_impl->panel)
    {
        auto *panel = new mviewer::ui::DisplayAdjustPanel(m_impl->host.data());
        m_impl->panel = panel;

        const mviewer::core::DisplayAdjust current =
            m_impl->current ? m_impl->current() : mviewer::core::DisplayAdjust{};
        panel->setExposure(current.exposureEv);
        panel->setGamma(current.gamma);
        panel->setAutoStretch(current.autoStretch);
        panel->setLutLabel(lutLabelFor(current.lutSize));

        connect(panel, &mviewer::ui::DisplayAdjustPanel::exposureChanged, this,
                &DisplayAdjustController::setExposure);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::gammaChanged, this,
                &DisplayAdjustController::setGamma);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::autoStretchToggled, this,
                &DisplayAdjustController::setAutoStretch);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::resetRequested, this,
                &DisplayAdjustController::resetToIdentity);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::loadLutRequested, this,
                &DisplayAdjustController::loadLut);

        bool anchorPlaced = false;
        // Hosts that lay out their own overlays (the image viewer, via
        // layoutFloatingOverlays) must not get an anchor: it would fight the
        // layout on every resize and drag the panel over the scope/spectrum.
        if (m_impl->autoAnchored)
        {
            m_impl->anchor = new AdjustAnchor(panel, m_impl->host.data());
            m_impl->host->installEventFilter(m_impl->anchor);
            anchorPlaced = true;
        }
        panel->show();
        if (anchorPlaced)
            m_impl->anchor->place();
    }
    else if (m_impl->panel)
    {
        m_impl->panel->setVisible(want);
    }
}

QString DisplayAdjustController::lutLabelFor(int lutSize)
{
    return lutSize > 0 ? tr("3D LUT: %1^3").arg(lutSize) : tr("Load 3D LUT...");
}

void DisplayAdjustController::setExposure(double exposureEv)
{
    if (m_impl->current)
    {
        mviewer::core::DisplayAdjust adjust = m_impl->current();
        adjust.exposureEv = exposureEv;
        m_impl->publish(adjust);
    }
}

void DisplayAdjustController::setGamma(double gamma)
{
    if (m_impl->current)
    {
        mviewer::core::DisplayAdjust adjust = m_impl->current();
        adjust.gamma = gamma;
        m_impl->publish(adjust);
    }
}

void DisplayAdjustController::setAutoStretch(bool on)
{
    if (m_impl->current)
    {
        mviewer::core::DisplayAdjust adjust = m_impl->current();
        adjust.autoStretch = on;
        m_impl->publish(adjust);
    }
}

void DisplayAdjustController::resetToIdentity()
{
    m_impl->publish(mviewer::core::DisplayAdjust{});
    syncPanelWidgets(mviewer::core::DisplayAdjust{});
}

void DisplayAdjustController::syncPanelWidgets(const mviewer::core::DisplayAdjust &adjust)
{
    // The panel only announces changes; it does not apply them. After a reset (or
    // when the host's adjust changed elsewhere) the sliders, the switch and the
    // LUT label would otherwise keep showing the old values.
    if (!m_impl->panel)
        return;
    const QSignalBlocker blocker(m_impl->panel);
    m_impl->panel->setExposure(adjust.exposureEv);
    m_impl->panel->setGamma(adjust.gamma);
    m_impl->panel->setAutoStretch(adjust.autoStretch);
    m_impl->panel->setLutLabel(lutLabelFor(adjust.lutSize));
}

void DisplayAdjustController::loadLut()
{
    if (!m_impl->host || !m_impl->current)
        return;
    const QString path = QFileDialog::getOpenFileName(
        m_impl->host.data(), tr("Load 3D LUT"), QString(),
        tr("Cube LUT (*.cube);;All files (*)"));
    if (path.isEmpty())
        return;
    mviewer::core::DisplayAdjust adjust = m_impl->current();
    std::string error;
    if (!mviewer::core::DisplayAdjust::loadCube(path.toUtf8().toStdString(), adjust, error))
    {
        QMessageBox::warning(m_impl->host.data(), tr("Load 3D LUT"),
                             tr("Could not load the 3D LUT: %1").arg(QString::fromStdString(error)));
        return;
    }
    m_impl->publish(adjust);
    if (m_impl->panel)
        m_impl->panel->setLutLabel(lutLabelFor(adjust.lutSize));
}

} // namespace mviewer::ui
