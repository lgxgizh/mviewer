#include "widgets/compareadjustcontroller.h"

#include "compareworkspace.h"
#include "core/image/DisplayAdjust.h"
#include "widgets/displayadjustpanel.h"

#include <QAction>
#include <QEvent>
#include <QFileDialog>
#include <QPointer>
#include <QString>

#include <functional>
#include <memory>

namespace mviewer::ui
{
namespace
{
// Pins the panel to the host's top-right corner: the fullscreen compare host is a
// plain QWidget, so there is no layout manager to do it.
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

class CompareAdjustController::Impl
{
  public:
    QPointer<QWidget> host;
    QPointer<CompareWorkspace> view;
    QPointer<QAction> action;
    QPointer<mviewer::ui::DisplayAdjustPanel> panel;
    QObject *anchor = nullptr;

    void apply(const std::function<void(mviewer::core::DisplayAdjust &)> &mutate) const
    {
        if (!view)
            return;
        auto next = view->displayColorContext();
        mutate(next.adjust);
        view->setDisplayColorContext(next);
    }
};

CompareAdjustController::CompareAdjustController(QObject *parent)
    : QObject(parent), m_impl(std::make_unique<Impl>())
{
}

CompareAdjustController::~CompareAdjustController() = default;

void CompareAdjustController::attach(QWidget *host, CompareWorkspace *view)
{
    m_impl->host = host;
    m_impl->view = view;
    m_impl->panel = nullptr;
    m_impl->anchor = nullptr;
    if (m_impl->action)
    {
        m_impl->action->setChecked(false);
        m_impl->action->setEnabled(view != nullptr && host != nullptr);
    }
}

void CompareAdjustController::setAction(QAction *action)
{
    m_impl->action = action;
    if (action)
        action->setEnabled(m_impl->view != nullptr && m_impl->host != nullptr);
}

bool CompareAdjustController::isPanelVisible() const
{
    return m_impl->panel && m_impl->panel->isVisible();
}

void CompareAdjustController::toggle()
{
    if (!m_impl->view || !m_impl->host)
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

        const mviewer::core::DisplayColorContext context = m_impl->view->displayColorContext();
        panel->setExposure(context.adjust.exposureEv);
        panel->setGamma(context.adjust.gamma);
        panel->setAutoStretch(context.adjust.autoStretch);
        panel->setLutLabel(context.adjust.lutSize > 0
                               ? tr("3D LUT: %1^3").arg(context.adjust.lutSize)
                               : tr("Load 3D LUT..."));

        connect(panel, &mviewer::ui::DisplayAdjustPanel::exposureChanged, this,
                &CompareAdjustController::setExposure);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::gammaChanged, this,
                &CompareAdjustController::setGamma);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::autoStretchToggled, this,
                &CompareAdjustController::setAutoStretch);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::resetRequested, this,
                &CompareAdjustController::resetToIdentity);
        connect(panel, &mviewer::ui::DisplayAdjustPanel::loadLutRequested, this,
                &CompareAdjustController::loadLut);

        auto *anchor = new AdjustAnchor(panel, m_impl->host.data());
        m_impl->anchor = anchor;
        m_impl->host->installEventFilter(anchor);
        panel->show();
        anchor->place();
    }
    else if (m_impl->panel)
    {
        m_impl->panel->setVisible(want);
    }
}

void CompareAdjustController::setExposure(double exposureEv)
{
    m_impl->apply([exposureEv](mviewer::core::DisplayAdjust &adjust) { adjust.exposureEv = exposureEv; });
}

void CompareAdjustController::setGamma(double gamma)
{
    m_impl->apply([gamma](mviewer::core::DisplayAdjust &adjust) { adjust.gamma = gamma; });
}

void CompareAdjustController::setAutoStretch(bool on)
{
    m_impl->apply([on](mviewer::core::DisplayAdjust &adjust) { adjust.autoStretch = on; });
}

void CompareAdjustController::resetToIdentity()
{
    m_impl->apply(
        [](mviewer::core::DisplayAdjust &adjust) { adjust = mviewer::core::DisplayAdjust{}; });
}

void CompareAdjustController::loadLut()
{
    if (!m_impl->view || !m_impl->host)
        return;
    const QString path = QFileDialog::getOpenFileName(
        m_impl->host.data(), tr("Load 3D LUT"), QString(), tr("Cube LUT (*.cube);;All files (*)"));
    if (path.isEmpty())
        return;
    auto next = m_impl->view->displayColorContext();
    std::string error;
    if (!mviewer::core::DisplayAdjust::loadCube(path.toUtf8().toStdString(), next.adjust, error))
        return; // the dialog already reported the reason; nothing to apply
    m_impl->view->setDisplayColorContext(next);
    if (m_impl->panel)
        m_impl->panel->setLutLabel(next.adjust.lutSize > 0
                                       ? tr("3D LUT: %1^3").arg(next.adjust.lutSize)
                                       : tr("Load 3D LUT..."));
}

} // namespace mviewer::ui
