#pragma once

#include "core/analysis/AnalysisEngine.h"
#include "core/analysis/ExportReport.h"
#include "core/compare/CompareEngine.h"
#include "core/compare/DifferenceEngine.h"
#include "core/compare/Histogram.h"
#include "core/image/ImageAdjust.h"
#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"

#include <QCheckBox>
#include <QComboBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QImage>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPixmap>
#include <QPointF>
#include <QPushButton>
#include <QResizeEvent>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QVector>
#include <QWidget>
#include <atomic>
#include <cstdint>
#include <memory>
#include <optional>
#include <vector>

class QScrollArea;
class QTableWidget;
class QComboBox;
class HistogramWidget;
class RawImageView;
class SelectionModel;

// CompareWorkspace：多图同步比较工作区
class CompareWorkspace : public QWidget
{
    Q_OBJECT

  public:
    explicit CompareWorkspace(QWidget *parent = nullptr);
    ~CompareWorkspace();

    void setImages(const QStringList &paths);

    bool isSyncEnabled() const;
    void setSyncEnabled(bool on);

    CompareEngine &engine()
    {
        return m_engine;
    }

    // M12.1: last ROI applied to the compare cells (for Workspace persistence).
    // Empty selection (width<=0) means no ROI was set.
    mviewer::domain::Selection currentROI() const
    {
        return m_lastSelection;
    }

    // M12.1: re-apply a persisted ROI (delegates to the internal all-cells apply).
    void applyROI(const mviewer::domain::Selection &sel)
    {
        applySelectionToAll(sel);
    }

    // M12.2 (G2-ext): the image paths currently loaded into the compare cells.
    // Used by Workspace persistence to capture session context per image.
    QStringList comparedImages() const;

    // M15 P0#1: full compare-session snapshot (sync mode, per-cell zoom/pan,
    // shared transform, ROI) plus the UI-only state (HeatMap threshold, blink
    // interval, side panel, layout combo) so a reopen fully restores the view.
    mviewer::domain::CompareSession compareSession() const
    {
        mviewer::domain::CompareSession s = m_engine.session();
        s.threshold = m_thresholdValue;
        s.blinkIntervalMs = m_blinkTimer ? m_blinkTimer->interval() : 150;
        s.sidePanelVisible = m_sideChk ? m_sideChk->isChecked() : false;
        s.layoutIndex = m_layoutCombo ? m_layoutCombo->currentIndex() : 0;
        s.uniformScale = m_uniformScale; // H5
        return s;
    }

    // M15: restore a persisted CompareSession: sync mode, shared zoom/pan, and
    // per-cell transforms. Call after setImages() so the engine owns the frames
    // the transforms reference. The selection/ROI is applied via applyROI().
    void applySession(const mviewer::domain::CompareSession &s);

    // M15 P0#1: number of images currently loaded into the comparison. Used by
    // the crash-recovery autosave to decide whether a Compare session is active.
    int comparedImageCount() const
    {
        return m_engine.imageCount();
    }

    // A-4.5: continuous compare — set the full image list so Next/Prev Pair
    // can walk through consecutive pairs without reopening the dialog.
    void setImagePool(const QStringList &allPaths);
    bool hasNextPair() const;
    bool hasPrevPair() const;

    // M20: named layout presets — 2-up (2×1), 4-up (2×2), 8-up (4×2).
    // Loads up to N images from the pool (or current set) and forces the grid.
    void applyLayoutPreset(int n); // n ∈ {2, 4, 8}
    // M20: window size for continuous navigation (2/4/8). Default 2 = pair.
    void setNavWindow(int n);
    int navWindow() const
    {
        return m_navWindow;
    }

    // P0: Inject the app-wide SelectionModel so that CompareWorkspace writes
    // the focused/reference image back to the global current image, keeping the
    // SSOT consistent across all panels.
    void setSelectionModel(SelectionModel *sel);

    // P1: Get the current focus image path (for triggering external analysis).
    QString focusImagePath() const;

    // Build one immutable report snapshot from every currently compared pane.
    mviewer::core::CompareReportBundle buildReportBundle() const;

  public slots:
    void nextPair();
    void prevPair();

  signals:
    void syncToggled(bool on);
    // Hover pixel read from any cell, formatted for the status bar. Empty string clears.
    void pixelInfo(const QString &text);
    // M24 (B#7): some of the requested images could not be loaded (missing /
    // corrupt / unsupported); the workspace kept the loadable ones. Text is a
    // user-facing explanation for the status bar.
    void loadWarning(const QString &text);
    // P1 #④: Trigger analysis of the current focus image from within Compare.
    void analyzeCurrent();
    // P1 #④: Trigger full report export from within Compare.
    void exportReportRequested();

  protected:
    void paintEvent(QPaintEvent *) override;
    bool eventFilter(QObject *, QEvent *) override;
    void resizeEvent(QResizeEvent *) override;
    void keyPressEvent(QKeyEvent *) override;
    void keyReleaseEvent(QKeyEvent *) override;
    void mousePressEvent(QMouseEvent *) override;
    void mouseMoveEvent(QMouseEvent *) override;
    void mouseReleaseEvent(QMouseEvent *) override;
    void leaveEvent(QEvent *) override;

  private:
    void rebuildCells();
    void fitAll();
    void applySelectionToAll(const mviewer::domain::Selection &sel);

    CompareEngine m_engine;

    // M28 P1-01: async image loading (decode happens on the DecodePool, never
    // on the UI thread). m_loadGen supersedes stale batches; a session applied
    // while a load is in flight is deferred until finishLoad().
    uint64_t m_loadGen = 0;
    bool m_loadInFlight = false;
    std::optional<mviewer::domain::CompareSession> m_pendingSession;
    void finishLoad(const std::vector<std::shared_ptr<ImageFrame>> &frames, int failedCount);
    QCheckBox *m_syncZoomChk = nullptr;
    QCheckBox *m_syncDragChk = nullptr;
    QCheckBox *m_uniformScaleChk = nullptr; // H5: 统一像素倍率
    bool m_syncZoom = true;
    bool m_syncDrag = true;
    bool m_uniformScale = false; // H5: force all panes to one shared zoom
    QWidget *m_grid = nullptr;
    QGridLayout *m_layout = nullptr;
    QList<QLabel *> m_cellLabels;
    QList<RawImageView *> m_cellViews;
    bool m_dragging = false;
    QPoint m_lastMouse;
    QPoint m_dragStartPos;
    int m_dragIdx = -1;
    mviewer::domain::Selection m_lastSelection; // M12.1: last applied ROI

    // M14-3 / P0-4: blink (flicker) compare
    QCheckBox *m_blinkChk = nullptr;
    QTimer *m_blinkTimer = nullptr;
    bool m_blinkState = false;
    bool m_tempBlinking = false; // true while Space is held down
    void toggleBlink();
    void applyBlink(bool state);
    void startBlink(int intervalMs);
    void stopBlink();
    // M24: mirror the blink target into the engine's BlinkController so the
    // captured CompareSession carries the blink state (round-trip persistence).
    void syncEngineBlink();
    bool isSplitOrSwipe() const
    {
        return (m_splitChk && m_splitChk->isChecked()) || (m_swipeChk && m_swipeChk->isChecked());
    }

    // P0-4: split / swipe compare (only meaningful for exactly two images).
    QCheckBox *m_splitChk = nullptr;
    QCheckBox *m_swipeChk = nullptr;
    double m_splitPos = 0.5;
    bool m_splitDragging = false;
    // A-4.1: Overlay compare mode — semi-transparent blend of the two images.
    QCheckBox *m_overlayChk = nullptr;
    QSlider *m_overlayAlphaSlider = nullptr; // 0–100 → opacity of top image
    QLabel *m_overlayAlphaLabel = nullptr;
    int m_overlayAlpha = 45; // percent
    void drawSplitCompare(QPainter &p);
    void drawSwipeCompare(QPainter &p, int x);
    void drawOverlayCompare(QPainter &p);
    void drawCellCompare(QPainter &p, int idx, const QRect &rect);

    // M23: checkerboard compare mode (棋盘格) — alternating A/B blocks.
    QCheckBox *m_checkerChk = nullptr;
    QSlider *m_checkerSizeSlider = nullptr;
    QLabel *m_checkerSizeLabel = nullptr;
    int m_checkerSize = 64; // block edge length in widget pixels
    void buildCheckerboardControls(QHBoxLayout *lay);
    void drawCheckerboardCompare(QPainter &p);
    bool anyCanvasCompareMode() const
    {
        return isSplitOrSwipe() || (m_overlayChk && m_overlayChk->isChecked()) ||
               (m_checkerChk && m_checkerChk->isChecked());
    }

    // A-4.3: Pixel Link — mark corresponding image-space points across cells.
    QCheckBox *m_pixelLinkChk = nullptr;
    QPushButton *m_clearLinksBtn = nullptr;
    QLabel *m_linkInfoLabel = nullptr;
    QVector<QPointF> m_linkPoints; // shared image-space markers (top-left origin)
    void onPixelLinkToggled(bool on);
    void addLinkPoint(const QPointF &imgPt);
    void clearLinkPoints();
    void refreshLinkMarkers();
    void updateLinkInfo();
    void drawPixelLinkLines(QPainter &p);

    // A-4.6: Diff highlight mode (red diffs / gray similar) vs heatmap.
    QCheckBox *m_diffHighlightChk = nullptr;
    bool m_diffHighlight = false;

    // A-4.2: custom M×N grid (spin boxes; 0 = use layout combo).
    QSpinBox *m_gridRowsSpin = nullptr;
    QSpinBox *m_gridColsSpin = nullptr;
    void onCustomGridChanged();

    // A-4.5 / M20: continuous compare — walk a sliding window over the pool.
    QStringList m_imagePool;
    int m_pairIndex = 0; // index of the first image of the current window
    int m_navWindow = 2; // 2 / 4 / 8 — step size for next/prev
    QPushButton *m_prevPairBtn = nullptr;
    QPushButton *m_nextPairBtn = nullptr;
    void updatePairButtons();
    // M20: snapshot UI mode so next/prev can restore after setImages rebuild.
    struct NavState
    {
        bool blink = false;
        bool split = false;
        bool swipe = false;
        bool overlay = false;
        bool checker = false; // M23
        int checkerSize = 64; // M23
        bool diffHighlight = false;
        bool syncZoom = true;
        bool syncDrag = true;
        bool crosshair = false;
        bool pixelLink = false;
        int overlayAlpha = 45;
        uint8_t threshold = 0;
        int layoutIndex = 0;
        mviewer::domain::Selection roi;
        bool hasRoi = false;
    };
    NavState captureNavState() const;
    void restoreNavState(const NavState &s);
    void showShortcutHelp();
    void exclusiveMode(QCheckBox *keepOn); // uncheck other exclusive modes

    // M15: difference threshold
    QSlider *m_thresholdSlider = nullptr;
    QLabel *m_thresholdLabel = nullptr;
    uint8_t m_thresholdValue = 0;

    // P0 #③: explicit multi-layout selector.
    QComboBox *m_layoutCombo = nullptr;
    void onLayoutChanged();

    // P0 #③: collapsible inspector + histogram side panel.
    QCheckBox *m_sideChk = nullptr;
    QWidget *m_sidePanel = nullptr;
    QTableWidget *m_inspector = nullptr;
    HistogramWidget *m_hist = nullptr;
    void onSideToggled(bool on);
    void updateInspector(int x, int y);
    // ── M30: coalesced Pixel Inspector hover path ──
    // High-frequency hover requests (RawImageView::pixelInfo plus the synced
    // crosshair pair) funnel into requestInspectorUpdate(): it stores the
    // latest coordinate immediately and schedules at most ONE zero-delay queued
    // render per event-loop turn. The queued callback is receiver-bound to
    // `this`, so it is dropped when the workspace is destroyed (no stale render
    // / use-after-free), and it always renders the CURRENT m_lastInspectX/Y
    // with the CURRENT semantic state — an old request can never overwrite
    // newer state. Low-frequency semantic changes (color space, kernel, focus,
    // delivered display image, edit target) funnel through the same coalescer
    // and are deterministically refreshed on the next event-loop turn.
    void requestInspectorUpdate(int x, int y);
    bool m_inspectQueued = false; // coalescing flag: one queued render at a time
    int m_inspectorSpaceIdx = -1; // last color-space index whose headers were set
    // M30 diagnostic render counter, exposed as the dynamic QObject property
    // "inspectorRenderCount" so the deterministic regression tests can observe
    // coalescing without widening the public API. Unsigned 64-bit: a very long
    // session must not let the counter sign-overflow.
    quint64 m_inspectorRenderCount = 0;
    void refreshHistograms();

    // ── M23: analysis panel (Pixel Inspector Pro + ROI histogram) ──
    // Built in compareworkspace_analysis.cpp per ADR 014 TU split.
    void buildAnalysisPanel(QVBoxLayout *sideLayout);
    QComboBox *m_csCombo = nullptr;     // inspector colour space selector
    QComboBox *m_kernelCombo = nullptr; // neighborhood kernel (1/3/5/7)
    QLabel *m_coordLabel = nullptr;     // hovered pixel coordinate readout
    QLabel *m_statsLabel = nullptr;     // neighborhood mean/σ of base cell
    QLabel *m_histTitle = nullptr;      // histogram section title (ROI aware)
    QCheckBox *m_histRChk = nullptr;
    QCheckBox *m_histGChk = nullptr;
    QCheckBox *m_histBChk = nullptr;
    QCheckBox *m_histLumaChk = nullptr;
    QCheckBox *m_histLogChk = nullptr;
    QCheckBox *m_roiHistChk = nullptr; // limit histogram to the current ROI

    // M16.1: cursor-sync crosshair (n/n) + focus-lock / reference pin (n/1).
    QCheckBox *m_crosshairChk = nullptr; // 同步准星开关
    QPushButton *m_focusBtn = nullptr;   // 锁定/解除基准
    QLabel *m_focusLabel = nullptr;      // 显示当前基准格
    int m_focusIndex = -1;               // 锁定的基准格索引 (-1 = 未锁定)
    int m_hoverIdx = -1;                 // 当前光标所在格 (用于锁定基准)
    int m_lastInspectX = -1;             // 最近检视位置 (焦点切换时重刷)
    int m_lastInspectY = -1;
    void onCrosshairMoved(RawImageView *view, const QPointF &pos);
    void onFocusRequested(int cellIndex);
    int diffBaseIndex() const
    {
        return m_focusIndex >= 0 ? m_focusIndex : 0;
    }

    // Repaints every diff overlay after a user-visible compare state change.
    void refreshAllDiffOverlays();

    // M29: async batch diff result — computed by a single Analysis-pool task
    // and delivered to the UI thread via qApp. Value/POD data only (QImage is
    // implicitly shared; no widget is referenced).
    struct DiffBatchResult
    {
        uint64_t generation = 0;
        int baseIdx = 0;
        int targetIdx = -1;
        bool sizeMismatch = false; // first non-base cell differs in size
        bool metricsValid = false;
        double psnr = 0.0;
        double ssim = 0.0;
        bool hasStats = false;
        DifferenceEngine::DiffStats stats;
        bool hasRoiStats = false;
        DifferenceEngine::DiffStats roiStats;

        struct CellOverlay
        {
            int index = -1;
            bool sizeMismatch = false;
            QImage overlay;
            double opacity = 0.5;
        };
        std::vector<CellOverlay> overlays;
    };
    void applyDiffBatchResult(const DiffBatchResult &result);

    // M29: latest-wins generation + handle of the in-flight batch diff task.
    uint64_t m_diffGen = 0;
    TaskScheduler::TaskHandle m_diffTask;
    // True while rebuildCells() is draining/recreating the panes. The terminal
    // refreshAllDiffOverlays() at the end of rebuildCells() covers the restored
    // ROI, so applySelectionToAll() skips scheduling its own refresh in that
    // window (avoids a duplicate batch submission).
    bool m_rebuildingCells = false;

    // M28 P1-01: async pane materialization. rebuildCells() and live adjustment
    // previews schedule ONE cancellable Analysis-pool task per request that
    // applies the captured CellAdjust and converts the owned result to QImage
    // off the UI thread, then marshals a value-only batch back to the UI thread
    // via qApp. Latest-wins: a newer schedule cancels the previous display task
    // and bumps the generation; the delivery is also guarded by generation and
    // pane count. Independent of the diff batch (m_diffGen/m_diffTask).
    struct DisplayBatchResult
    {
        uint64_t generation = 0;
        int paneCount = 0;

        struct CellImage
        {
            int index = -1;
            QImage image;
        };
        std::vector<CellImage> cells;
    };
    void scheduleDisplayMaterialization(const std::vector<int> &dirtyPanes);
    void applyDisplayBatchResult(const DisplayBatchResult &result);

    uint64_t m_displayGen = 0;
    TaskScheduler::TaskHandle m_displayTask;

    // ── M16.2: per-cell image adjustments ──
    struct CellAdjust
    {
        int brightness = 0;    // [-255, 255]
        float contrast = 1.0f; // [0, 3.0]
        float gamma = 1.0f;    // [0.05, 8.0]
        float rGain = 1.0f;    // WB red gain [0.01, 5.0]
        float bGain = 1.0f;    // WB blue gain [0.01, 5.0]
        int rotation = 0;      // 0, 90, 180, 270
        bool hasCrop = false;
        int cropX = 0, cropY = 0, cropW = 0, cropH = 0;

        bool isIdentity() const
        {
            return brightness == 0 && std::abs(contrast - 1.0f) < 1e-6f &&
                   std::abs(gamma - 1.0f) < 1e-6f && std::abs(rGain - 1.0f) < 1e-6f &&
                   std::abs(bGain - 1.0f) < 1e-6f && rotation == 0 && !hasCrop;
        }
    };
    std::vector<CellAdjust> m_cellAdjusts; // per-cell adjustment state
    int m_editIdx = -1;                    // currently selected cell for editing
    static ImageData applyAdjusts(const ImageData &src, const CellAdjust &a);

    // Edit panel widgets (inside side panel)
    QWidget *m_editPanel = nullptr;
    QLabel *m_editLabel = nullptr; // shows which cell is being edited
    QSlider *m_brightSlider = nullptr;
    QLabel *m_brightVal = nullptr;
    QSlider *m_contrastSlider = nullptr;
    QLabel *m_contrastVal = nullptr;
    QSlider *m_gammaSlider = nullptr;
    QLabel *m_gammaVal = nullptr;
    QSlider *m_rGainSlider = nullptr;
    QLabel *m_rGainVal = nullptr;
    QSlider *m_bGainSlider = nullptr;
    QLabel *m_bGainVal = nullptr;
    QPushButton *m_resetAdjBtn = nullptr;
    void onEditCellSelected(int cellIdx);
    void onAdjChanged();
    void onResetAdj();
    void buildEditPanel(QVBoxLayout *sideLayout);
    void applyAdjToCell(int cellIdx);

    // ── M16.4: quick PSNR/SSIM metrics ──
    QLabel *m_metricLabel = nullptr;

    // ── M16.7: adjusted-aware diff/metrics + per-pane histogram overlay ──
    ImageData adjustedPixels(int cellIdx) const;
    void onAdjEditFinished();
    void refreshCellHist(int idx);
    void positionCellHists();
    void onPaneHistOverlayToggled(bool on);
    bool m_paneHistOverlay = false;

    // Async batch histogram refresh: the per-pane overlays and the main analysis
    // histogram are computed by ONE cancellable Analysis-pool task per logical
    // refresh and delivered to the UI thread via qApp. Value/POD data only — no
    // widget, QObject, ImageFrame, or CompareEngine reference crosses the thread
    // boundary. Latest-wins: a newer schedule cancels the previous task and bumps
    // the generation; delivery re-checks both. Independent of the display
    // (m_displayGen/m_displayTask) and diff (m_diffGen/m_diffTask) batches.
    struct HistogramBatchResult
    {
        uint64_t generation = 0;
        int paneCount = 0;
        bool updateMain = false;
        // M23: value-only main-title state captured at schedule time, so the
        // title and the histogram data are delivered as one coherent pair —
        // a pending/rejected batch can never show a new ROI title over old data.
        bool roiEnabled = false;
        mviewer::domain::Selection roi;
        std::vector<mviewer::core::Histogram> main; // main surface, in main order

        struct CellHist
        {
            int index = -1;
            mviewer::core::Histogram hist;
        };
        std::vector<CellHist> panes; // pane overlay widgets keyed by index
    };
    void scheduleHistogramRefresh(bool includeMain, const std::vector<int> &paneIndices);
    void applyHistogramBatchResult(const HistogramBatchResult &result);
    // M23: the histogram section title text for the given ROI/full state.
    QString histogramTitleText(bool roiEnabled, const mviewer::domain::Selection &roi) const;

    uint64_t m_histGen = 0;
    TaskScheduler::TaskHandle m_histTask;
    QCheckBox *m_paneHistOverlayChk = nullptr;
    std::vector<HistogramWidget *> m_cellHists;

    // ── M16.5: per-pane histogram overlay toggle ──
    bool m_perPaneHist = false;
    QCheckBox *m_perPaneHistChk = nullptr;
    void onPerPaneHistToggled(bool on);

    // ── M16.6: layout presets save/load ──
    QPushButton *m_savePresetBtn = nullptr;
    QPushButton *m_loadPresetBtn = nullptr;
    QString m_presetDir;
    void onSavePreset();
    void onLoadPreset();
    void ensurePresetDir();

    // ── M16.6: swap panes ──
    QPushButton *m_swapBtn = nullptr;
    void onSwapPanes();

    // P0: App-wide SelectionModel for current-image consistency.
    SelectionModel *m_selection = nullptr;

    // P1 #④: Compare → Analyze → Export workflow buttons.
    QPushButton *m_analyzeBtn = nullptr;
    QPushButton *m_exportReportBtn = nullptr;
};
