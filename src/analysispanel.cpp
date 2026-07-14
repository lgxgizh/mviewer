#include "analysispanel.h"

#include "core/image/QtConvert.h"

#include <QFont>
#include <QHBoxLayout>
#include <QPainter>
#include <QVBoxLayout>

AnalysisPanel::AnalysisPanel(QWidget *parent) : QWidget(parent) {
  buildUi();
  setMinimumWidth(360);
  setMinimumHeight(480);
}

void AnalysisPanel::buildUi() {
  QVBoxLayout *mainLay = new QVBoxLayout(this);
  mainLay->setContentsMargins(6, 6, 6, 6);
  mainLay->setSpacing(4);

  // Plugin selector
  QHBoxLayout *plugBar = new QHBoxLayout;
  plugBar->addWidget(new QLabel(tr("Analyzer:")));
  m_analyzerCombo = new QComboBox;
  m_analyzerCombo->addItem(tr("Built-in Histogram+Stats"), QString("builtin"));
  m_analyzerCombo->addItem(tr("Dual Compare (PSNR/SSIM)"),
                           QString("builtin_compare"));
  auto &reg = AnalyzerRegistry::instance();
  m_pluginIds = reg.availableAnalyzers();
  for (const auto &id : m_pluginIds) {
    m_analyzerCombo->addItem(QString::fromStdString("Plugin: " + id),
                             QString("plugin"));
  }
  plugBar->addWidget(m_analyzerCombo, 1);
  mainLay->addLayout(plugBar);

  connect(m_analyzerCombo, QOverload<int>::of(&QComboBox::activated), this,
          &AnalysisPanel::onAnalyzerSelected);

  m_tabs = new QTabWidget;
  mainLay->addWidget(m_tabs, 1);

  // Histogram tab: viz + stats text
  m_histogramLabel = new QLabel;
  m_histogramLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_histogramLabel->setStyleSheet("QLabel{background:#141414;}");
  m_statsLabel = new QLabel;
  m_statsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_statsLabel->setWordWrap(true);
  m_statsLabel->setStyleSheet(
      "QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
  auto *histPage = new QWidget;
  auto *histLay = new QVBoxLayout(histPage);
  histLay->setContentsMargins(0, 0, 0, 0);
  histLay->setSpacing(4);
  histLay->addWidget(m_histogramLabel, 1);
  histLay->addWidget(m_statsLabel);
  m_tabs->addTab(histPage, tr("Histogram"));

  // Compare tab
  m_compareLabel = new QLabel;
  m_compareLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_compareLabel->setWordWrap(true);
  m_compareLabel->setStyleSheet(
      "QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
  m_tabs->addTab(m_compareLabel, tr("Compare"));

  // Diff preview
  m_diffPreview = new QLabel;
  m_diffPreview->setMinimumHeight(kPreviewSize);
  m_diffPreview->setAlignment(Qt::AlignCenter);
  m_diffPreview->setStyleSheet("QLabel{background:#1e1e1e;}");
  m_tabs->addTab(m_diffPreview, tr("Diff Map"));

  // Plugin tab
  m_pluginResult = new QLabel;
  m_pluginResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
  m_pluginResult->setWordWrap(true);
  m_pluginResult->setStyleSheet(
      "QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
  m_tabs->addTab(m_pluginResult, tr("Plugin"));

  m_analyzerCombo->setCurrentIndex(0);
  onAnalyzerSelected(0);
}

void AnalysisPanel::setImage(const QImage &img) {
  if (img.isNull()) {
    clear();
    return;
  }
  m_imageA = img.convertToFormat(QImage::Format_RGB32);
  m_hasA = true;
  m_hasB = false;
  m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
  updateHistogramPage();
}

void AnalysisPanel::setImages(const QImage &a, const QImage &b) {
  if (a.isNull() || b.isNull())
    return;
  m_imageA = a.convertToFormat(QImage::Format_RGB32);
  m_imageB = b.convertToFormat(QImage::Format_RGB32);
  m_hasA = m_hasB = true;
  m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
  m_statsB = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageB));
  updateComparePage();
}

void AnalysisPanel::clear() {
  m_imageA = m_imageB = QImage();
  m_hasA = m_hasB = false;
  m_statsA = m_statsB = ImageStats();
  m_hasROI = false;
  m_statsLabel->clear();
  m_compareLabel->clear();
  m_diffPreview->clear();
  m_pluginResult->clear();
  m_histogramLabel->clear();
}

void AnalysisPanel::setROI(const mviewer::domain::Selection &roi) {
  m_roi = roi;
  m_hasROI = !roi.isEmpty();
  if (m_hasA && m_hasROI) {
    m_statsA =
        AnalysisEngine::computeStatsROI(mvcore::fromQImage(m_imageA), roi);
    updateHistogramPage();
  }
}

void AnalysisPanel::setRegionStats(const QString &text) {
  m_statsLabel->setText(
      QString("<h3>%1</h3><p>%2</p>").arg(tr("Region Stats")).arg(text));
}

void AnalysisPanel::onAnalyzerSelected(int index) {
  m_currentPluginIdx = index;
  if (index == 0)
    updateHistogramPage();
  else if (index == 1)
    updateComparePage();
  else
    updatePluginPage();
}

void AnalysisPanel::updateHistogramPage() {
  if (!m_hasA) {
    m_statsLabel->setText(tr("No image selected"));
    return;
  }
  QString title = m_hasROI ? tr("ROI Stats") : tr("Full Image Stats");
  QString txt = QString("<h3>%1</h3>").arg(title);
  txt += QString("<table>"
                 "<tr><td>%2</td><td>%3</td></tr>"
                 "<tr><td>%4</td><td>%5</td></tr>"
                 "<tr><td>%6</td><td>%7</td></tr>"
                 "<tr><td>%8</td><td>%9</td></tr>"
                 "<tr><td>%10</td><td>%11</td></tr>"
                 "</table>")
             .arg(tr("Lum Mean"))
             .arg(m_statsA.lumMean, 0, 'f', 2)
             .arg(tr("R Mean"))
             .arg(m_statsA.rMean, 0, 'f', 2)
             .arg(tr("G Mean"))
             .arg(m_statsA.gMean, 0, 'f', 2)
             .arg(tr("B Mean"))
             .arg(m_statsA.bMean, 0, 'f', 2)
             .arg(tr("Pixels"))
             .arg(m_statsA.pixelCount);
  m_statsLabel->setText(txt);
  renderHistogramPixmap();
}

void AnalysisPanel::renderHistogramPixmap() {
  if (!m_hasA)
    return;
  const int W = qMax(200, m_histogramLabel->width() - 8);
  const int H = 160;
  QPixmap pix(W, H);
  pix.fill(QColor(20, 20, 20));
  QPainter p(&pix);
  const int pad = 4;
  const QRect bg(pad, pad, W - pad * 2, H - pad * 2);
  // Overlaid 4 channels
  auto drawChannel = [&bg, &p](const int *hist, const QColor &color) {
    constexpr int srcBins = 256;
    constexpr int drawBins = 64;
    const double binW = static_cast<double>(bg.width()) / drawBins;
    long long agg[drawBins] = {0};
    long long maxV = 1;
    for (int i = 0; i < drawBins; ++i) {
      long long sum = 0;
      const int lo = i * srcBins / drawBins;
      const int hi = (i + 1) * srcBins / drawBins;
      for (int j = lo; j < hi && j < srcBins; ++j)
        sum += hist[j];
      agg[i] = sum;
      if (sum > maxV)
        maxV = sum;
    }
    p.setPen(color);
    for (int i = 0; i < drawBins; ++i) {
      const double h = static_cast<double>(agg[i]) / maxV * bg.height();
      const int x = bg.x() + static_cast<int>(i * binW);
      const int hh = qMax(1, static_cast<int>(h));
      p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
    }
  };
  drawChannel(m_statsA.histLum, QColor(220, 220, 220));
  drawChannel(m_statsA.histR, QColor(230, 70, 70));
  drawChannel(m_statsA.histG, QColor(70, 220, 70));
  drawChannel(m_statsA.histB, QColor(70, 130, 230));
  m_histogramLabel->setPixmap(pix);
}

void AnalysisPanel::updateComparePage() {
  if (!m_hasA || !m_hasB) {
    m_compareLabel->setText(tr("Need two images to compare"));
    return;
  }
  double psnr = AnalysisEngine::psnr(mvcore::fromQImage(m_imageA),
                                     mvcore::fromQImage(m_imageB));
  double ssim = AnalysisEngine::ssim(mvcore::fromQImage(m_imageA),
                                     mvcore::fromQImage(m_imageB));
  double noiseA = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));
  double noiseB = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageB));

  QString txt = QString("<h3>%1</h3>").arg(tr("Dual Compare"));
  txt += QString("<table>"
                 "<tr><td>%2</td><td>%3 dB</td></tr>"
                 "<tr><td>%4</td><td>%5</td></tr>"
                 "<tr><td>%6</td><td>%7</td></tr>"
                 "<tr><td>%8</td><td>%9</td></tr>"
                 "</table>")
             .arg(tr("PSNR"))
             .arg(psnr, 0, 'f', 2)
             .arg(tr("SSIM"))
             .arg(ssim, 0, 'f', 4)
             .arg(tr("Noise(A)"))
             .arg(noiseLevelText(noiseA))
             .arg(tr("Noise(B)"))
             .arg(noiseLevelText(noiseB));
  m_compareLabel->setText(txt);

  QImage diff = computeDifferencePreview(m_imageA, m_imageB);
  if (!diff.isNull()) {
    m_diffPreview->setPixmap(QPixmap::fromImage(diff).scaled(
        QSize(kPreviewSize, kPreviewSize), Qt::KeepAspectRatio,
        Qt::SmoothTransformation));
  }
}

void AnalysisPanel::updatePluginPage() {
  if (m_pluginIds.empty()) {
    m_pluginResult->setText(tr("No analyzer plugins available"));
    return;
  }
  int pluginIdx = m_currentPluginIdx - 2;
  if (pluginIdx < 0 || pluginIdx >= static_cast<int>(m_pluginIds.size())) {
    m_pluginResult->setText(tr("Select a plugin"));
    return;
  }
  const std::string &id = m_pluginIds[pluginIdx];
  auto &reg = AnalyzerRegistry::instance();
  auto analyzer = reg.create(id);
  if (!analyzer) {
    m_pluginResult->setText(
        tr("Cannot create: %1").arg(QString::fromStdString(id)));
    return;
  }
  QString txt = QString("<h3>%1</h3><p>%2</p>")
                    .arg(QString::fromStdString(analyzer->name()))
                    .arg(QString::fromStdString(analyzer->description()));
  m_pluginResult->setText(txt);
}

QImage AnalysisPanel::computeDifferencePreview(const QImage &a,
                                               const QImage &b) {
  ImageData diff = AnalysisEngine::differenceMap(mvcore::fromQImage(a),
                                                 mvcore::fromQImage(b));
  if (diff.isNull())
    return QImage();
  return mvcore::toQImage(diff);
}

QString AnalysisPanel::noiseLevelText(double variance) {
  if (variance < 50)
    return tr("Very Low (%1)").arg(variance, 0, 'f', 1);
  if (variance < 150)
    return tr("Low (%1)").arg(variance, 0, 'f', 1);
  if (variance < 300)
    return tr("Medium (%1)").arg(variance, 0, 'f', 1);
  if (variance < 500)
    return tr("High (%1)").arg(variance, 0, 'f', 1);
  return tr("Very High (%1)").arg(variance, 0, 'f', 1);
}

void AnalysisPanel::paintEvent(QPaintEvent *event) {
  QWidget::paintEvent(event);
  // Histogram viz rendered via QPixmap in renderHistogramPixmap()
}
