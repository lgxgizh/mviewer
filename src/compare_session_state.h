#pragma once

#include "core/analysis/ImageOverlay.h"
#include "domain/CompareSession.h"
#include "domain/Selection.h"

#include <QObject>
#include <QPointF>
#include <QStringList>
#include <vector>

namespace mviewer::ui
{

// Component extracted from CompareWorkspace god-class (ADR-014 componentization).
// Owns and encapsulates compare session parameters, image list, frame identities,
// transform state, and layout presets.
class CompareSessionState : public QObject
{
    Q_OBJECT

  public:
    explicit CompareSessionState(QObject *parent = nullptr);
    ~CompareSessionState() override = default;

    // Compared paths and frames
    const QStringList &comparePaths() const noexcept { return m_comparePaths; }
    void setComparePaths(const QStringList &paths);

    const std::vector<int> &frameIndices() const noexcept { return m_frameIndices; }
    void setFrameIndices(const std::vector<int> &indices);

    int count() const noexcept { return m_comparePaths.size(); }
    bool isEmpty() const noexcept { return m_comparePaths.isEmpty(); }

    // Navigation and pooling
    int navWindow() const noexcept { return m_navWindow; }
    void setNavWindow(int window);

    const QStringList &allPoolPaths() const noexcept { return m_allPoolPaths; }
    void setImagePool(const QStringList &allPaths, const QStringList &currentWindow = {});

    // Layout preset (2, 4, 8)
    int layoutPreset() const noexcept { return m_layoutPreset; }
    void setLayoutPreset(int preset);

    // Sync mode & transforms
    bool isSyncEnabled() const noexcept { return m_syncEnabled; }
    void setSyncEnabled(bool enabled);

    bool autoAlignBeforeDiff() const noexcept { return m_autoAlignBeforeDiff; }
    void setAutoAlignBeforeDiff(bool on);

    double sharedScale() const noexcept { return m_sharedScale; }
    void setSharedScale(double s) noexcept { m_sharedScale = s; }

    const QPointF &sharedOffset() const noexcept { return m_sharedOffset; }
    void setSharedOffset(const QPointF &offset) noexcept { m_sharedOffset = offset; }

    // Active ROI
    const mviewer::domain::Selection &currentROI() const noexcept { return m_currentROI; }
    void setCurrentROI(const mviewer::domain::Selection &roi);
    void clearROI();

    // Overlays
    mviewer::OverlayMode overlayMode() const noexcept { return m_overlayMode; }
    void setOverlayMode(mviewer::OverlayMode mode);

    // Snapshot & Restore
    mviewer::domain::CompareSession toDomainSession() const;
    void restoreFromDomainSession(const mviewer::domain::CompareSession &session);

  signals:
    void pathsChanged(const QStringList &paths);
    void roiChanged(const mviewer::domain::Selection &roi);
    void syncToggled(bool enabled);
    void layoutPresetChanged(int preset);
    void overlayModeChanged(mviewer::OverlayMode mode);
    void autoAlignToggled(bool on);

  private:
    QStringList m_comparePaths;
    std::vector<int> m_frameIndices;
    int m_navWindow = 2;
    QStringList m_allPoolPaths;
    int m_layoutPreset = 2;
    bool m_syncEnabled = true;
    bool m_autoAlignBeforeDiff = false;
    double m_sharedScale = 1.0;
    QPointF m_sharedOffset{0.0, 0.0};
    mviewer::domain::Selection m_currentROI;
    mviewer::OverlayMode m_overlayMode = mviewer::OverlayMode::None;
};

} // namespace mviewer::ui
