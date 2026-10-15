#pragma once

#include <QObject>
#include <QSize>
#include <algorithm>
#include <utility>
#include <vector>

namespace mviewer::ui
{

// Component extracted from CompareWorkspace god-class (ADR-014 componentization).
// Manages grid layout configuration, pane column counts, and viewport synchronization
// (Pan / Zoom sync, uniform pixel scale calculation, fit calculations).
class CompareDisplayManager : public QObject
{
    Q_OBJECT

  public:
    explicit CompareDisplayManager(QObject *parent = nullptr);
    ~CompareDisplayManager() override = default;

    int columns() const noexcept { return m_columns; }
    void setColumns(int cols);

    bool uniformScale() const noexcept { return m_uniformScale; }
    void setUniformScale(bool on);

    bool syncZoom() const noexcept { return m_syncZoom; }
    void setSyncZoom(bool on);

    bool syncDrag() const noexcept { return m_syncDrag; }
    void setSyncDrag(bool on);

    double sharedZoomRatio() const noexcept { return m_sharedZoomRatio; }
    void setSharedZoomRatio(double r) noexcept { m_sharedZoomRatio = r; }

    const std::vector<double> &fitScales() const noexcept { return m_fitScales; }
    void setFitScales(std::vector<double> scales);

    // Pure geometric helpers
    static double computeFitScale(int viewW, int viewH, int imgW, int imgH) noexcept;
    static double computeFitScale(const QSize &viewSize, const QSize &imgSize) noexcept;
    static double computeUniformFitScale(const std::vector<QSize> &viewSizes,
                                         const std::vector<QSize> &imgSizes) noexcept;
    static std::pair<int, int> computeGridRowsCols(int imageCount, int targetCols) noexcept;

  signals:
    void columnsChanged(int cols);
    void uniformScaleToggled(bool on);
    void syncZoomToggled(bool on);
    void syncDragToggled(bool on);
    void fitRequested();

  private:
    int m_columns = 2;
    bool m_uniformScale = false;
    bool m_syncZoom = true;
    bool m_syncDrag = true;
    double m_sharedZoomRatio = 1.0;
    std::vector<double> m_fitScales;
};

} // namespace mviewer::ui
