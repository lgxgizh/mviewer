#include "analysispanel.h"

#include "core/image/QtConvert.h"

#include <QPainter>
#include <QVBoxLayout>
#include <QHBoxLayout>
#include <QGroupBox>
#include <QScrollArea>
#include <QFont>
#include <QPalette>

AnalysisPanel::AnalysisPanel(QWidget *parent)
    : QWidget(parent)
{
    buildUi();
    setMinimumWidth(360);
    setMinimumHeight(480);
}

void AnalysisPanel::buildUi()
{
    QVBoxLayout *mainLay = new QVBoxLayout(this);
    mainLay->setContentsMargins(6, 6, 6, 6);
    mainLay->setSpacing(4);

    // 插件选择器
    QHBoxLayout *plugBar = new QHBoxLayout;
    plugBar->addWidget(new QLabel(tr("分析器:")));
    m_analyzerCombo = new QComboBox;
    // 添加内置分析器
    m_analyzerCombo->addItem(tr("内建直方图+统计"), QString("builtin"));
    m_analyzerCombo->addItem(tr("双图比较 (PSNR/SSIM)"), QString("builtin_compare"));
    // 添加注册的分析器
    auto &reg = AnalyzerRegistry::instance();
    m_pluginIds = reg.availableAnalyzers();
    for (const auto &id : m_pluginIds) {
        m_analyzerCombo->addItem(QString::fromStdString("Plugin: " + id), QString("plugin"));
    }
    plugBar->addWidget(m_analyzerCombo, 1);
    mainLay->addLayout(plugBar);

    connect(m_analyzerCombo, QOverload<int>::of(&QComboBox::activated),
            this, &AnalysisPanel::onAnalyzerSelected);

    // Tab 页面
    m_tabs = new QTabWidget;
    mainLay->addWidget(m_tabs, 1);

    // 直方图页
    m_statsLabel = new QLabel;
    m_statsLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_statsLabel->setWordWrap(true);
    m_statsLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_statsLabel, tr("直方图"));

    // 比较页
    m_compareLabel = new QLabel;
    m_compareLabel->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_compareLabel->setWordWrap(true);
    m_compareLabel->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_compareLabel, tr("比较"));

    // 差异预览
    m_diffPreview = new QLabel;
    m_diffPreview->setMinimumHeight(kPreviewSize);
    m_diffPreview->setAlignment(Qt::AlignCenter);
    m_diffPreview->setStyleSheet("QLabel{background:#1e1e1e;}");
    m_tabs->addTab(m_diffPreview, tr("差异图"));

    // 插件页
    m_pluginResult = new QLabel;
    m_pluginResult->setAlignment(Qt::AlignTop | Qt::AlignLeft);
    m_pluginResult->setWordWrap(true);
    m_pluginResult->setStyleSheet("QLabel{background:#1e1e1e;color:#eee;padding:8px;}");
    m_tabs->addTab(m_pluginResult, tr("插件"));

    // 默认选中直方图
    m_analyzerCombo->setCurrentIndex(0);
    onAnalyzerSelected(0);
}

void AnalysisPanel::setImage(const QImage &img)
{
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

void AnalysisPanel::setImages(const QImage &a, const QImage &b)
{
    if (a.isNull() || b.isNull()) return;
    m_imageA = a.convertToFormat(QImage::Format_RGB32);
    m_imageB = b.convertToFormat(QImage::Format_RGB32);
    m_hasA = true;
    m_hasB = true;
    m_statsA = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageA));
    m_statsB = AnalysisEngine::computeStats(mvcore::fromQImage(m_imageB));
    updateComparePage();
}

void AnalysisPanel::clear()
{
    m_imageA = QImage();
    m_imageB = QImage();
    m_hasA = false;
    m_hasB = false;
    m_statsA = ImageStats();
    m_statsB = ImageStats();
    m_hasROI = false;
    m_statsLabel->clear();
    m_compareLabel->clear();
    m_diffPreview->clear();
    m_pluginResult->clear();
}

void AnalysisPanel::setROI(const mviewer::domain::Selection &roi)
{
    m_roi = roi;
    m_hasROI = !roi.isEmpty();
    if (m_hasA && m_hasROI) {
        m_statsA = AnalysisEngine::computeStatsROI(mvcore::fromQImage(m_imageA), roi);
        updateHistogramPage();
    }
}

void AnalysisPanel::onAnalyzerSelected(int index)
{
    m_currentPluginIdx = index;
    if (index == 0) {
        updateHistogramPage();
    } else if (index == 1) {
        updateComparePage();
    } else {
        updatePluginPage();
    }
}

void AnalysisPanel::updateHistogramPage()
{
    if (!m_hasA) {
        m_statsLabel->setText(tr("未选择图片"));
        return;
    }
    QString title = m_hasROI ? tr("框选区域统计") : tr("全图统计");
    QString txt = QString("<h3>%1</h3>").arg(title);
    txt += QString("<p>"
                   "<table>"
                   "<tr><td>%2</td><td>%3</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "<tr><td>%8</td><td>%9</td></tr>"
                   "<tr><td>%10</td><td>%11</td></tr>"
                   "</table></p>")
            .arg(tr("亮度均值")).arg(m_statsA.lumMean, 0, 'f', 2)
            .arg(tr("R 均值")).arg(m_statsA.rMean, 0, 'f', 2)
            .arg(tr("G 均值")).arg(m_statsA.gMean, 0, 'f', 2)
            .arg(tr("B 均值")).arg(m_statsA.bMean, 0, 'f', 2)
            .arg(tr("像素数")).arg(m_statsA.pixelCount);

    // 直方图绘制
    m_statsLabel->setText(txt);
    update();
}

void AnalysisPanel::updateComparePage()
{
    if (!m_hasA || !m_hasB) {
        m_compareLabel->setText(tr("需要两张图片进行比较"));
        return;
    }
    double psnr = AnalysisEngine::psnr(mvcore::fromQImage(m_imageA), mvcore::fromQImage(m_imageB));
    double ssim = AnalysisEngine::ssim(mvcore::fromQImage(m_imageA), mvcore::fromQImage(m_imageB));
    double noiseA = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageA));
    double noiseB = AnalysisEngine::noiseEstimate(mvcore::fromQImage(m_imageB));

    QString txt = QString("<h3>%1</h3>").arg(tr("双图比较"));
    txt += QString("<p>"
                   "<table>"
                   "<tr><td>%2</td><td>%3 dB</td></tr>"
                   "<tr><td>%4</td><td>%5</td></tr>"
                   "<tr><td>%6</td><td>%7</td></tr>"
                   "<tr><td>%8</td><td>%9</td></tr>"
                   "</table></p>")
            .arg(tr("PSNR")).arg(psnr, 0, 'f', 2)
            .arg(tr("SSIM")).arg(ssim, 0, 'f', 4)
            .arg(tr("噪声(A)")).arg(noiseLevelText(noiseA))
            .arg(tr("噪声(B)")).arg(noiseLevelText(noiseB));

    m_compareLabel->setText(txt);

    // 差异图预览
    QImage diff = computeDifferencePreview(m_imageA, m_imageB);
    if (!diff.isNull()) {
        m_diffPreview->setPixmap(QPixmap::fromImage(diff).scaled(
            QSize(kPreviewSize, kPreviewSize), Qt::KeepAspectRatio, Qt::SmoothTransformation));
    }
    update();
}

void AnalysisPanel::updatePluginPage()
{
    if (m_pluginIds.empty()) {
        m_pluginResult->setText(tr("没有可用的分析器插件"));
        return;
    }
    int pluginIdx = m_currentPluginIdx - 2; // 前两个是内置
    if (pluginIdx < 0 || pluginIdx >= static_cast<int>(m_pluginIds.size())) {
        m_pluginResult->setText(tr("请选择一个插件"));
        return;
    }
    const std::string &id = m_pluginIds[pluginIdx];
    auto &reg = AnalyzerRegistry::instance();
    auto analyzer = reg.create(id);
    if (!analyzer) {
        m_pluginResult->setText(tr("无法创建分析器: %1").arg(QString::fromStdString(id)));
        return;
    }
    QString txt = QString("<h3>%1</h3><p>%2</p>")
            .arg(QString::fromStdString(analyzer->name()))
            .arg(QString::fromStdString(analyzer->description()));
    m_pluginResult->setText(txt);
}

QImage AnalysisPanel::computeDifferencePreview(const QImage &a, const QImage &b)
{
    ImageData diff = AnalysisEngine::differenceMap(mvcore::fromQImage(a), mvcore::fromQImage(b));
    if (diff.isNull()) return QImage();
    return mvcore::toQImage(diff);
}

QString AnalysisPanel::noiseLevelText(double variance)
{
    if (variance < 50) return tr("极低 (%1)").arg(variance, 0, 'f', 1);
    if (variance < 150) return tr("低 (%1)").arg(variance, 0, 'f', 1);
    if (variance < 300) return tr("中 (%1)").arg(variance, 0, 'f', 1);
    if (variance < 500) return tr("高 (%1)").arg(variance, 0, 'f', 1);
    return tr("极高 (%1)").arg(variance, 0, 'f', 1);
}

void AnalysisPanel::paintEvent(QPaintEvent *event)
{
    QWidget::paintEvent(event);
    if (m_currentPluginIdx != 0 || !m_hasA) return;

    QPainter painter(this);
    // 在直方图页绘制直方图
    // 注意：实际绘制在 updateHistogramPage 中通过 update() 触发
    // 这里简化处理，使用 m_statsLabel 的 pixmap 或直接绘制
    // 由于布局复杂，直方图绘制在独立的 QLabel 中进行
}

void AnalysisPanel::drawHistogramChannel(QPainter &p, const QRect &bg, const int *hist, const QColor &color)
{
    const int bins = kHistBins;
    const int srcBins = 256;
    const double binW = static_cast<double>(bg.width()) / bins;

    long long agg[kHistBins] = {0};
    long long maxV = 1;
    for (int i = 0; i < bins; ++i) {
        long long sum = 0;
        const int lo = i * srcBins / bins;
        const int hi = (i + 1) * srcBins / bins;
        for (int j = lo; j < hi && j < srcBins; ++j)
            sum += hist[j];
        agg[i] = sum;
        if (sum > maxV) maxV = sum;
    }

    p.setPen(color);
    for (int i = 0; i < bins; ++i) {
        const double h = static_cast<double>(agg[i]) / maxV * bg.height();
        const int x = bg.x() + static_cast<int>(i * binW);
        const int hh = std::max(1, static_cast<int>(h));
        p.drawLine(x, bg.bottom(), x, bg.bottom() - hh);
    }
}

void AnalysisPanel::drawThinHistogram(QPainter &p, const QRect &bg, const int *hist, const QColor &color)
{
    drawHistogramChannel(p, bg, hist, color);
}

void AnalysisPanel::setRegionStats(const QString &text)
{
    // 兼容旧接口：直接显示文本
    m_statsLabel->setText(QString("<h3>%1</h3><p>%2</p>")
            .arg(tr("区域统计"))
            .arg(text));
}
