#pragma once

#include <QDialog>
#include <QImage>

class QComboBox;
class QSlider;
class QLabel;

// F4 (M22): visual analysis overlays + scopes for the current image.
//   - Zebra: hatch over/under-exposed pixels (threshold adjustable).
//   - False-color: map luma through a perceptual colormap.
//   - Waveform (RGB parade) + Vectorscope: chroma/luma scopes.
// Self-contained dialog opened from the main window on the current image.
class AnalysisOverlayDialog : public QDialog
{
    Q_OBJECT
  public:
    explicit AnalysisOverlayDialog(const QImage &image, QWidget *parent = nullptr);

  private slots:
    void updateOverlay();

  private:
    QImage m_src;
    QComboBox *m_mode = nullptr;
    QSlider *m_threshold = nullptr;
    QLabel *m_preview = nullptr;
    QWidget *m_scope = nullptr;
};
