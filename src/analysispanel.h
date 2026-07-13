#pragma once

#include <QWidget>
#include <QImage>
#include <QString>
#include <QRect>
#include <QTabWidget>
#include <QComboBox>
#include <QLabel>

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "domain/Selection.h"

// AnalysisPanel：多模式分析面板
//  - 直方图+统计（单图）
//  - 框选区域统计（ROI）
//  - 双图比较（PSNR/SSIM/差异图/噪声）
//  - 支持 AnalyzerRegistry 插件扩展
class AnalysisPanel : public QWidget
{
    Q_OBJECT

public:
    explicit AnalysisPanel(QWidget *parent = nullptr);

    void setImage(const QImage &img);
    void setImages(const QImage &a, const QImage &b);
    void clear();

    // ROI 区域设置（图像坐标系）
    void setROI(const mviewer::domain::Selection &roi);

    // 向后兼容：显示任意文本区域统计(来自 ImageViewer::regionStats 信号)
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
    void drawHistogramChannel(QPainter &p, const QRect &bg, const int *hist, const QColor &color);
    void drawThinHistogram(QPainter &p, const QRect &bg, const int *hist, const QColor &color);
    QImage computeDifferencePreview(const QImage &a, const QImage &b);
    QString noiseLevelText(double variance);

    enum Page { HistogramPage, ComparePage, PluginPage };

    // UI
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_analyzerCombo = nullptr;
    QLabel *m_statsLabel = nullptr;
    QLabel *m_compareLabel = nullptr;
    QLabel *m_diffPreview = nullptr;
    QLabel *m_pluginResult = nullptr;

    // 数据
    QImage m_imageA;
    QImage m_imageB;
    bool m_hasA = false;
    bool m_hasB = false;
    ImageStats m_statsA;
    ImageStats m_statsB;
    mviewer::domain::Selection m_roi;
    bool m_hasROI = false;

    // 插件
    std::vector<std::string> m_pluginIds;
    int m_currentPluginIdx = -1;

    static constexpr int kHistBins = 128;
    static constexpr int kPreviewSize = 128;
};
