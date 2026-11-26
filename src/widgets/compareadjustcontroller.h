#pragma once

// Shared display adjustment for the two compare panes.
//
// The "Compare Adjust" subsystem pulled out of MainWindow: one panel, owned and
// anchored by this object, driving a CompareWorkspace's DisplayColorContext.
// Keeping it here leaves MainWindow holding a single unique_ptr instead of a
// panel plus an anchor plus five signal handlers.

#include <QObject>
#include <QString>

#include <memory>

class QAction;
class QWidget;

class CompareWorkspace; // global namespace (see compareworkspace.h)

namespace mviewer::ui
{

class CompareAdjustController final : public QObject
{
    Q_OBJECT

  public:
    CompareAdjustController(QObject *parent = nullptr);
    ~CompareAdjustController() override;

    /// Bind to the fullscreen compare host and its workspace. Call again when a
    /// new compare session starts; the previous panel is released.
    void attach(QWidget *host, CompareWorkspace *view);

    void toggle();
    bool isPanelVisible() const;

    /// The action that flips the panel. The controller keeps its enabled/checked
    /// state in step with the host lifetime.
    void setAction(QAction *action);

  public Q_SLOTS:
    void setExposure(double exposureEv);
    void setGamma(double gamma);
    void setAutoStretch(bool on);
    void resetToIdentity();
    void loadLut();

  private:
    class Impl;
    std::unique_ptr<Impl> m_impl;
};

} // namespace mviewer::ui
