#pragma once

#include "core/image/ImageBuffer.h"

#include <QImage>
#include <QObject>
#include <cstdint>

namespace mviewer::ui
{

enum class DiffBlendMode : uint8_t
{
    Difference,
    Heatmap,
    SideBySideSplit,
    Swipe
};

// Component extracted from CompareWorkspace god-class (ADR-014 componentization).
// Responsible for diff overlay calculation, swipe/split viewport blending, and blink state.
class CompareDiffRenderer : public QObject
{
    Q_OBJECT

  public:
    explicit CompareDiffRenderer(QObject *parent = nullptr);
    ~CompareDiffRenderer() override = default;

    // Threshold (0..255)
    uint8_t threshold() const noexcept { return m_threshold; }
    void setThreshold(uint8_t th);

    // Split / Swipe parameters
    double splitRatio() const noexcept { return m_splitRatio; }
    void setSplitRatio(double ratio);

    Qt::Orientation splitOrientation() const noexcept { return m_splitOrientation; }
    void setSplitOrientation(Qt::Orientation orient);

    // Blend mode
    DiffBlendMode blendMode() const noexcept { return m_blendMode; }
    void setBlendMode(DiffBlendMode mode);

    // Blink state
    bool isBlinkActive() const noexcept { return m_blinkActive; }
    void setBlinkActive(bool active);

    int blinkIntervalMs() const noexcept { return m_blinkIntervalMs; }
    void setBlinkIntervalMs(int ms);

    // Pure rendering algorithms
    static QImage renderSplit(const QImage &imgA, const QImage &imgB, double ratio,
                              Qt::Orientation orientation = Qt::Horizontal);

    static ImageData computeDifference(const ImageData &imgA, const ImageData &imgB,
                                       uint8_t threshold);

  signals:
    void thresholdChanged(uint8_t th);
    void splitRatioChanged(double ratio);
    void blendModeChanged(DiffBlendMode mode);
    void blinkStateChanged(bool active);

  private:
    uint8_t m_threshold = 0;
    double m_splitRatio = 0.5;
    Qt::Orientation m_splitOrientation = Qt::Vertical;
    DiffBlendMode m_blendMode = DiffBlendMode::Difference;
    bool m_blinkActive = false;
    int m_blinkIntervalMs = 150;
};

} // namespace mviewer::ui
