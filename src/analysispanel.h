#pragma once

#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QRect>
#include <QString>
#include <QTabWidget>
#include <QWidget>

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "domain/Selection.h"

// AnalysisPanel: multi-mode analysis panel
//  - Histogram + stats (single image)
//  - ROI stats
//  - Dual-image compare (PSNR/SSIM/Noise/Diff)
//  - AnalyzerRegistry plugin extensibility
class AnalysisPanel : public QWidget {
  Q_OBJECT

public:
  explicit AnalysisPanel(QWidget *parent = nullptr);

  void setImage(const QImage &img);
  void setImages(const QImage &a, const QImage &b);
  void clear();

  // ROI (image coordinates)
  void setROI(const mviewer::domain::Selection &roi);

  // Backward-compat: display arbitrary region-stats text (from
  // ImageViewer::regionStats)
  void setRegionStats(const QString &text);

public slots:
  void onAnalyzerSelected(int index);

protected:
  void paintEvent(QPaintEvent *) override;

private:
  void buildUi();
  void updateHistogramPage();
  void updateComparePage();
  void updatePluginPage();
  void renderHistogramPixmap();
  QImage computeDifferencePreview(const QImage &a, const QImage &b);
  QString noiseLevelText(double variance);

  enum Page { HistogramPage, ComparePage, PluginPage };

  // UI
  QTabWidget *m_tabs = nullptr;
  QComboBox *m_analyzerCombo = nullptr;
  QLabel *m_histogramLabel =
      nullptr; // histogram viz (replaces dead drawHistogramChannel)
  QLabel *m_statsLabel = nullptr;
  QLabel *m_compareLabel = nullptr;
  QLabel *m_diffPreview = nullptr;
  QLabel *m_pluginResult = nullptr;

  // Data
  QImage m_imageA;
  QImage m_imageB;
  bool m_hasA = false;
  bool m_hasB = false;
  ImageStats m_statsA;
  ImageStats m_statsB;
  mviewer::domain::Selection m_roi;
  bool m_hasROI = false;

  // Plugins
  std::vector<std::string> m_pluginIds;
  int m_currentPluginIdx = -1;

  static constexpr int kPreviewSize = 192;
};
