#pragma once

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/PixelInspector.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/image/ImageFrame.h"
#include "core/scheduler/TaskScheduler.h"
#include "domain/Selection.h"

#include <QComboBox>
#include <QImage>
#include <QLabel>
#include <QPixmap>
#include <QPointer>
#include <QRect>
#include <QString>
#include <QTabWidget>
#include <QWidget>

#include <cstdint>
#include <memory>

class RawImageView;
class AnalyzerModel;
class QListWidget;
class QPushButton;
class QVBoxLayout;

// Complete types are defined in analysispanel.cpp (worker payload).
struct AnalysisInput;
struct AnalysisResult;

// AnalysisPanel: multi-mode analysis panel
//  - Histogram + stats (single image)
//  - ROI stats
//  - Dual-image compare (PSNR/SSIM/Noise/Diff)
//  - AnalyzerRegistry plugin extensibility
//  - M21: Analysis History + Pinned Result via AnalyzerModel
class AnalysisPanel : public QWidget
{
    Q_OBJECT

  public:
    // The report boundary must distinguish a result produced by the current
    // request from an older result retained by AnalyzerModel for history.  In
    // particular, NoResult and Unavailable are terminal states that must not
    // fall back to that retained text during export.
    enum class ReportAnalysisState
    {
        Unset,
        Pending,
        Available,
        NoResult,
        Unavailable
    };

    explicit AnalysisPanel(QWidget *parent = nullptr);
    ~AnalysisPanel() override;

    // M15 P0#3: inject the analyzer pipeline (orchestration layer over the
    // AnalyzerRegistry). The panel routes all analyzer creation/execution
    // through this pipeline instead of touching the registry directly, so the
    // MainWindow -> Analyzer coupling is removed. Adding a new analyzer only
    // needs registration in the AnalyzerFactory; MainWindow/Panel stay
    // unchanged (acceptance: "新增 Analyzer 时 MainWindow 0 修改").
    void setPipeline(std::unique_ptr<AnalyzerPipeline> pipeline)
    {
        m_pipeline = std::move(pipeline);
    }

    // M21: inject the shared AnalyzerModel (history / pin / result SSOT).
    // The panel writes results on reanalyze() and reads history/pin for UI.
    void setAnalyzerModel(AnalyzerModel *model);

    void setImage(const QImage &img);
    void setImage(const QImage &img, const QString &path);
    // M28 P1-02: true when the panel has materialized the frame into its
    // display/analysis buffers. A hidden panel defers this to showEvent, so
    // opening the viewer no longer pays full-image conversions for a panel
    // the user cannot see.
    bool hasLoadedImage() const
    {
        return m_hasA && !m_imageA.isNull();
    }
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
    // A-7.2: rebuild the analyzer combo from the current pipeline/registry so
    // runtime-loaded plugins appear without restarting the app.
    void refreshAnalyzers();
    // A-7.1 / A-7.3: select an analyzer by registry id and run it. Used by the
    // viewer context-menu "分析" submenu so every analyzer enters through the
    // same panel path (combo + Result tab), not a one-off dialog.
    void selectAnalyzer(const QString &id);
    void runAnalyzer(const QString &id);
    // Backward-compat: display arbitrary region-stats text (from
    // ImageViewer::regionStats)
    void setRegionStats(const QString &text);

    // M12.1: last analysis result text (for Workspace persistence).
    QString analysisText() const
    {
        return m_pluginResult ? m_pluginResult->text() : QString();
    }

    ReportAnalysisState reportAnalysisState() const
    {
        return m_reportAnalysisState;
    }
    bool reportAnalysisPending() const
    {
        return m_reportAnalysisState == ReportAnalysisState::Pending;
    }
    QString reportAnalyzerId() const
    {
        return m_reportAnalyzerId;
    }

    // Pixel Inspector (M3 Phase-2): live readout of the hovered pixel.
    // `left*` are the RGB read directly from the ImageFrame (passed by the
    // viewer). When a second image is loaded, `right*` come from the compare
    // image so the panel can show Left RGB / Right RGB / Delta / Difference.
    void showPixel(int x, int y, int leftR, int leftG, int leftB, int leftA, int r16, int g16,
                   int b16, int rawKind, bool valid);

    // Set the left image as an ImageFrame so ROI analysis routes through the
    // AnalyzerRegistry (Selection-based), not the legacy QImage path.
    void setFrame(std::shared_ptr<ImageFrame> frame);

  public slots:
    void onAnalyzerSelected(int index);
    void updateImage(const QImage &img);
    void updateHistogram(const mviewer::domain::Histogram &hist);

  signals:
    // P1-6: one-click "export report" entry inside the analysis panel itself,
    // so the analysis workflow doesn't force the user into a menu. The MainWindow
    // wires this to its report-exporter for the current image.
    void exportRequested();
    // M21: user picked a history/pinned entry — MainWindow should open that image.
    void historyImageRequested(const QString &path);

  protected:
    // M28 P1-02: materialize a deferred frame when the panel becomes visible.
    void showEvent(QShowEvent *event) override;
    void paintEvent(QPaintEvent *event) override;
    void renderHistogramPixmap();
    void renderHistogramPixmap(const mviewer::domain::Histogram &hist);

  private:
    void buildUi();
    void buildAnalyzerSection(QVBoxLayout &layout);
    void buildHistorySection(QVBoxLayout &layout);
    void buildResultTabs(QVBoxLayout &layout);
    void buildInspectorTab();
    void buildInspectorActions(QVBoxLayout &layout);
    void updateHistogramPage();
    void updateComparePage();
    void updatePluginPage();
    void updateInspectorPage();
    void updateRgbPage();
    void updateExposurePage();
    void updateFocusPage();
    void updateMetadataPage();
    bool runLegacyAnalyzer(const QString &id);
    void runRoiAnalysis();
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
    QLabel *m_rgbLabel = nullptr;      // P1-1: RGB channel viz
    QLabel *m_rgbStatsLabel = nullptr; // P1-1: RGB stats text
    QLabel *m_exposureLabel = nullptr; // P1-1: exposure stats
    QLabel *m_focusLabel = nullptr;    // P1-1: focus / sharpness stats
    QLabel *m_metaLabel = nullptr;     // P1-1: metadata summary
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
    // M28 P1-02/P1-04 + manual rerun: ONE panel-owned task + ONE monotonic
    // generation own BOTH modes of async analysis:
    //   - materializing refresh: setFrame stores the shared frame and marks it
    //     dirty; a HIDDEN panel submits no work until shown; showing (or a
    //     visible panel's next event-loop turn) schedules exactly one
    //     cancellable, latest-wins AnalysisPool task. The worker materializes
    //     the RGB32 image, computes base stats + noise once, and executes the
    //     selected analyzer OFF the UI thread; the queued delivery renders the
    //     base pages from the full-image stats.
    //   - analyzer-only rerun: once the frame is loaded (materialized + base
    //     stats done), every user-triggered single-frame analysis (combo
    //     change / ROI change / runAnalyzer / manual reanalyze) schedules a
    //     latest-wins AnalysisPool task that only executes the selected
    //     analyzer (and, on a no-result, the legacy ROI fallback over the
    //     shared materialized image). It never re-materializes the frame or
    //     recomputes base stats/noise, and the call returns immediately.
    // A stale async delivery is discarded unless its generation and frame
    // identity (and, for analyzer-only jobs, the publish path) still match.
    // Legacy explicit paths (setImage/setImages/clear) cancel/invalidate any
    // in-flight task so a stale async delivery can never overwrite them; the
    // QImage-only panel (no valid ImageFrame) keeps its synchronous behavior.
    bool m_frameDirty = false;
    void invalidateAnalysis();
    void scheduleAnalysis();
    bool scheduleAnalyzerRun();
    void applyAnalysisResult(const AnalysisResult &result);
    void resetImagePresentation();
    void applyFrameImage(const QImage &rgb32, const QString &path);
    QString publishPath() const;
    void renderAnalyzerOutcome(const AnalysisResult &result);
    void renderRoiOutcome(const AnalysisResult &result);
    void renderNoResult();
    void renderAnalysisUnavailable();
    void clearAnalyzerResultSurface();
    void showAnalysisPending();
    void setReportAnalysisState(ReportAnalysisState state, const QString &producerAnalyzerId = {});
    // Submit a snapshot to the Analysis pool and marshal the worker result back
    // onto the UI thread (defined in analysispanel.cpp next to runAnalysis).
    static TaskScheduler::TaskHandle submitAnalysisJob(const AnalysisInput &in,
                                                       QPointer<AnalysisPanel> guard);
    std::shared_ptr<ImageFrame> m_frameA; // left image frame for ROI analysis
    TaskScheduler::TaskHandle m_task; // owned handle of the current analysis job
    uint64_t m_gen = 0;               // monotonically increasing generation (latest-wins)
    // Cached noise estimate so updateFocusPage never re-scans the image. The
    // async worker and the legacy synchronous paths both populate it before the
    // cheap page updates run.
    double m_noiseA = 0.0;
    bool m_noiseValid = false;

    // Pixel Inspector last sample
    int m_px = -1, m_py = -1;
    int m_pR = 0, m_pG = 0, m_pB = 0;
    bool m_pValid = false;
    bool m_frozen = false; // Freeze: keep showing the last inspected pixel
    // P0-2/PixelInspector: original high-bit-depth sample (from ImageViewer).
    int m_pA = 0;
    int m_r16 = 0, m_g16 = 0, m_b16 = 0;
    uint16_t m_rawMax = 0;
    int m_rawKind = 0; // 0=8-bit, 1=RAW preview, 2=true 16-bit

    // M15 P0 #2: Pixel Inspector Pro — selected color space + NxN kernel.
    mviewer::core::ColorSpace m_colorSpace = mviewer::core::ColorSpace::RGB;
    int m_kernel = 1; // 1,3,5,7 → 1×1,3×3,5×5,7×7

    // Plugins
    std::vector<std::string> m_pluginIds;
    int m_currentPluginIdx = -1;

    // M15 P0#3: orchestration layer, owned solely by this panel. Nullable so
    // headless/tests can still fall back to the registry (see reanalyze() /
    // buildUi()).
    std::unique_ptr<AnalyzerPipeline> m_pipeline;

    // M21: shared result/history/pin model (not owned).
    AnalyzerModel *m_analyzerModel = nullptr;
    QListWidget *m_historyList = nullptr;
    QListWidget *m_pinnedList = nullptr;
    QPushButton *m_pinBtn = nullptr;
    QPushButton *m_exportButton = nullptr;
    ReportAnalysisState m_reportAnalysisState = ReportAnalysisState::Unset;
    QString m_reportAnalyzerId;
    void refreshHistoryUi();
    void refreshPinnedUi();
    void onHistoryActivated();
    void onPinnedActivated();
    void onPinToggled();
    void publishResult(const QString &plainText, const QString &producerAnalyzerId = {});

    static constexpr int kPreviewSize = 192;
};
