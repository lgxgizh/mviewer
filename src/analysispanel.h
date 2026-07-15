#pragma once

#include "core/analysis/AnalysisEngine.h"
#include "core/analyzer/Analyzer.h"
#include "core/image/ImageFrame.h"
#include "domain/Selection.h"

#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QRect>
#include <QString>
#include <QTabWidget>
#include <QWidget>
#include <memory>

class RawImageView;

// AnalysisPanel: multi-mode analysis panel
//  - Histogram + stats (single image)
//  - ROI stats
//  - Dual-image compare (PSNR/SSIM/Noise/Diff)
//  - AnalyzerRegistry plugin extensibility
class AnalysisPanel : public QWidget
{
Q_OBJECT

public:
    explicit AnalysisPanel(QWidget* parent = nullptr);

    void setImage(const QImage& img);
    void setImages(const QImage& a, const QImage& b);
    void clear();

    // ROI (image coordinates)
    void setROI(const mviewer::domain::Selection& roi);

    // Backward-compat: display arbitrary region-stats text (from
    // ImageViewer::regionStats)
    void setRegionStats(const QString& text);

    // Pixel Inspector (M3 Phase-2): live readout of the hovered pixel.
    // `left*` are the RGB read directly from the ImageFrame (passed by the
    // viewer). When a second image is loaded, `right*` come from the compare
    // image so the panel can show Left RGB / Right RGB / Delta / Difference.
    void showPixel(int x, int y, int leftR, int leftG, int leftB, bool valid);

    // Set the left image as an ImageFrame so ROI analysis routes through the
    // AnalyzerRegistry (Selection-based), not the legacy QImage path.
    void setFrame(std::shared_ptr<ImageFrame> frame);

public slots:
    void onAnalyzerSelected(int index);
    void updateImage(const QImage& img);
    void updateHistogram(const mviewer::domain::Histogram& hist);

protected:
    void paintEvent(QPaintEvent*) override;

private:
    void buildUi();
    void updateHistogramPage();
    void updateComparePage();
    void updatePluginPage();
    void updateInspectorPage();
    void renderHistogramPixmap();
    void renderHistogramPixmap(const mviewer::domain::Histogram& hist);
    QImage computeDifferencePreview(const QImage& a, const QImage& b);
    QString noiseLevelText(double variance);

    enum Page
    {
        HistogramPage,
        ComparePage,
        PluginPage,
        InspectorPage
    };

    // UI
    QTabWidget* m_tabs = nullptr;
    QComboBox* m_analyzerCombo = nullptr;
    QLabel* m_histogramLabel = nullptr; // histogram viz (replaces dead drawHistogramChannel)
    QLabel* m_statsLabel = nullptr;
    QLabel* m_compareLabel = nullptr;
    QLabel* m_diffPreview = nullptr;
    QLabel* m_pluginResult = nullptr;
    QLabel* m_inspectorLabel = nullptr; // Pixel Inspector readout
    std::unique_ptr<RawImageView> m_imageView;

    // Data
    QImage m_imageA;
    QImage m_imageB;
    bool m_hasA = false;
    bool m_hasB = false;
    ImageStats m_statsA;
    ImageStats m_statsB;
    mviewer::domain::Selection m_roi;
    bool m_hasROI = false;
    std::shared_ptr<ImageFrame> m_frameA; // left image frame for ROI analysis

    // Pixel Inspector last sample
    int m_px = -1, m_py = -1;
    int m_pR = 0, m_pG = 0, m_pB = 0;
    bool m_pValid = false;

    // Plugins
    std::vector<std::string> m_pluginIds;
    int m_currentPluginIdx = -1;

    static constexpr int kPreviewSize = 192;
};
