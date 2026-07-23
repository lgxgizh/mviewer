#pragma once

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/image/ImageFrame.h"
#include "domain/Selection.h"

#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QPixmap>
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
    explicit AnalysisPanel(QWidget *parent = nullptr);

    // M15 P0#3: inject the analyzer pipeline (orchestration layer over the
    // AnalyzerRegistry). The panel routes all analyzer creation/execution
    // through this pipeline instead of touching the registry directly, so the
    // MainWindow -> Analyzer coupling is removed. Adding a new analyzer only
    // needs registration in the AnalyzerFactory; MainWindow/Panel stay
    // unchanged (acceptance: "新增 Analyzer 时 MainWindow 0 修改").
    void setPipeline(std::shared_ptr<AnalyzerPipeline> pipeline)
    {
        m_pipeline = std::move(pipeline);
    }

    void setImage(const QImage &img);
    void setImage(const QImage &img, const QString &path);
    void setImages(const QImage &a, const QImage &b);
    void clear();

    // M14-4: expose the histogram pixmap for report export.
    QPixmap histogramPixmap() const
    {
        return m_histogramLabel ? m_histogramLabel->pixmap() : QPixmap();
    }

    // ROI (image coordinates)
    void setROI(const mviewer::domain::Selection &roi);

    // P1-3: read/restore the active Analysis sub-page (Histogram/RGB/Noise/...).
    int currentPage() const;
    void setCurrentPage(int index);
    // Re-run the currently-selected registry analyzer over the left frame + ROI.
    void reanalyze();
    // Backward-compat: display arbitrary region-stats text (from
    // ImageViewer::regionStats)
    void setRegionStats(const QString &text);

    // M12.1: last analysis result text (for Workspace persistence).
    QString analysisText() const
    {
        return m_pluginResult ? m_pluginResult->text() : QString();
    }

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
    void updateImage(const QImage &img);
    void updateHistogram(const mviewer::domain::Histogram &hist);

  protected:
    void paintEvent(QPaintEvent *event) override;
    void renderHistogramPixmap();
    void renderHistogramPixmap(const mviewer::domain::Histogram &hist);

  private:
    void buildUi();
    void updateHistogramPage();
    void updateComparePage();
    void updatePluginPage();
    void updateInspectorPage();
    void updateRgbPage();
    void updateExposurePage();
    void updateFocusPage();
    void updateMetadataPage();
    void renderChannel(QLabel *label, const int *hist, const QColor &color);
    QImage computeDifferencePreview(const QImage &a, const QImage &b);
    QString noiseLevelText(double variance);

    enum Page
    {
        HistogramPage,
        RgbPage,
        ExposurePage,
        FocusPage,
        MetadataPage,
        ComparePage,
        DiffMapPage,
        PluginPage,
        InspectorPage
    };

    // UI
    QTabWidget *m_tabs = nullptr;
    QComboBox *m_analyzerCombo = nullptr;
    QLabel *m_histogramLabel = nullptr; // histogram viz (replaces dead drawHistogramChannel)
    QLabel *m_statsLabel = nullptr;
    QLabel *m_rgbLabel = nullptr;       // P1-1: RGB channel viz
    QLabel *m_rgbStatsLabel = nullptr;  // P1-1: RGB stats text
    QLabel *m_exposureLabel = nullptr;  // P1-1: exposure stats
    QLabel *m_focusLabel = nullptr;     // P1-1: focus / sharpness stats
    QLabel *m_metaLabel = nullptr;      // P1-1: metadata summary
    QLabel *m_compareLabel = nullptr;
    QLabel *m_diffPreview = nullptr;
    QLabel *m_pluginResult = nullptr;
    QLabel *m_inspectorLabel = nullptr; // Pixel Inspector readout
    std::unique_ptr<RawImageView> m_imageView;

    // Data
    QImage m_imageA;
    QImage m_imageB;
    QString m_imagePath; // P1-1: source path for metadata extraction
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

    // M15 P0 #2: Pixel Inspector Pro — selected color space + NxN kernel.
    mviewer::core::ColorSpace m_colorSpace = mviewer::core::ColorSpace::RGB;
    int m_kernel = 1; // 1,3,5,7 → 1×1,3×3,5×5,7×7

    // Plugins
    std::vector<std::string> m_pluginIds;
    int m_currentPluginIdx = -1;

    // M15 P0#3: orchestration layer. Nullable so headless/tests can still fall
    // back to the registry (see reanalyze() / buildUi()).
    std::shared_ptr<AnalyzerPipeline> m_pipeline;

    static constexpr int kPreviewSize = 192;
};
