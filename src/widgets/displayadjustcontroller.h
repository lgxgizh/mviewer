#pragma once

// Shared display-adjust widget for the two places that show a live image: the
// full-image viewer and the fullscreen compare host.
//
// The panel itself was already reusable; this object owns it, anchors it to the
// host, and turns its signals into "apply this DisplayAdjust" callbacks. Both
// hosts have to rebuild their own DisplayColorContext (the adjust is part of the
// cache key), so the caller supplies how to read the current value and how to
// publish a new one. Before this existed the two hosts carried a panel, an anchor
// and five nearly identical signal handlers each.

#include <QObject>
#include <QString>

#include <functional>

class QAction;
class QWidget;

namespace mviewer::core
{
struct DisplayAdjust;
}

namespace mviewer::ui
{

class DisplayAdjustController final : public QObject
{
    Q_OBJECT

  public:
    using CurrentFn = std::function<mviewer::core::DisplayAdjust()>;
    using ApplyFn = std::function<void(const mviewer::core::DisplayAdjust &)>;

    explicit DisplayAdjustController(QObject *parent = nullptr);
    ~DisplayAdjustController() override;

    /// Bind to a host widget and the read/publish callbacks. Passing nulls (or
    /// attaching again) releases the previous panel.
    void attach(QWidget *host, CurrentFn current, ApplyFn apply);

    void detach();

    /// Re-publish the currently displayed adjustment. Used when a caller mutates
    /// the adjust in place and then asks for it to be applied.
    void applyCurrent();

    void toggle();
    bool isPanelVisible() const;
    /// The panel, when created (nullptr before the first show). Lets a host raise
    /// or reposition it without owning it.
    QWidget *panelWidget() const;

    /// Pin the panel to the host's top-right corner and follow resizes. Hosts that
    /// lay out their own overlays (the image viewer) pass false.
    void setAutoAnchored(bool anchored);

    void setAction(QAction *action);

  public Q_SLOTS:
    void setExposure(double exposureEv);
    void setGamma(double gamma);
    void setAutoStretch(bool on);
    void resetToIdentity();
    void loadLut();

  private:
    static QString lutLabelFor(int lutSize);
    /// Push values into the panel without letting it re-announce them.
    void syncPanelWidgets(const mviewer::core::DisplayAdjust &adjust);
    class Impl;
    Impl *m_impl = nullptr;
};

} // namespace mviewer::ui
