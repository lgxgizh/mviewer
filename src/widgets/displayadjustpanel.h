#pragma once

#include <QWidget>

class QPushButton;
class QSlider;
class QLabel;

namespace mviewer::ui
{

// Live review adjustment controls (report feature 5): exposure, gamma,
// auto-stretch and a 3D LUT. The panel only mutates a
// mviewer::core::DisplayAdjust; the viewer pushes it into the display colour
// context, which invalidates the raster/tile caches and re-materializes.
class DisplayAdjustPanel : public QWidget
{
    Q_OBJECT

  public:
    explicit DisplayAdjustPanel(QWidget *parent = nullptr);

    void setExposure(double ev);
    void setGamma(double gamma);
    void setAutoStretch(bool on);
    void setLutLabel(const QString &label);

  signals:
    void exposureChanged(double ev);
    void gammaChanged(double gamma);
    void autoStretchToggled(bool on);
    void loadLutRequested();
    void resetRequested();

  private:
    void updateReadout();

    QSlider *m_exposure = nullptr;
    QSlider *m_gamma = nullptr;
    QPushButton *m_autoStretch = nullptr;
    QPushButton *m_lut = nullptr;
    QPushButton *m_reset = nullptr;
    QLabel *m_readout = nullptr;
};

} // namespace mviewer::ui
