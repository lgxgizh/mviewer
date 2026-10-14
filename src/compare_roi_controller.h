#pragma once

#include "compareworkspace_roi_types.h"
#include "domain/Selection.h"

#include <QObject>

namespace mviewer::ui
{

// Component extracted from CompareWorkspace god-class (ADR-014 componentization).
// Responsible for linked ROI selection, incremental nudging, and measurement telemetry state.
class CompareRoiController : public QObject
{
    Q_OBJECT

  public:
    explicit CompareRoiController(QObject *parent = nullptr);
    ~CompareRoiController() override = default;

    const mviewer::domain::Selection &currentROI() const noexcept { return m_roi; }
    bool hasValidROI() const noexcept { return !m_roi.isEmpty(); }

    void setROI(const mviewer::domain::Selection &sel);
    void clearROI();
    void nudgeROI(int dx, int dy, int maxWidth = 0, int maxHeight = 0);

    // Measurement & Telemetry state tracking
    ROIMeasurementState measurementState() const noexcept { return m_state; }
    void setMeasurementState(ROIMeasurementState state);

    uint64_t currentGeneration() const noexcept { return m_generation; }
    uint64_t bumpGeneration() noexcept { return ++m_generation; }

  signals:
    void roiChanged(const mviewer::domain::Selection &roi);
    void roiCleared();
    void measurementStateChanged(ROIMeasurementState state);

  private:
    mviewer::domain::Selection m_roi;
    ROIMeasurementState m_state = ROIMeasurementState::Idle;
    uint64_t m_generation = 0;
};

} // namespace mviewer::ui
