#pragma once

#include "core/compare/CompareEngine.h"
#include "core/image/ImageAdjust.h"
#include "core/image/ImageBuffer.h"
#include "core/analysis/AnalysisEngine.h"

#include <QCheckBox>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QLabel>
#include <QMap>
#include <QMouseEvent>
#include <QPushButton>
#include <QPixmap>
#include <QPointF>
#include <QScrollArea>
#include <QSlider>
#include <QSpinBox>
#include <QStringList>
#include <QTimer>
#include <QComboBox>
#include <QWidget>
#include <memory>
#include <vector>

class QScrollArea;
class QTableWidget;
class QComboBox;
class HistogramWidget;
class RawImageView;

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
        s.blinkIntervalMs = m_blinkTimer ? m_blinkTimer->interval() : 500;
        s.sidePanelVisible = m_sideChk ? m_sideChk->isChecked() : false;
        s.layoutIndex = m_layoutCombo ? m_layoutCombo->currentIndex() : 0;
        return s;
    }

    // M15: restore a persisted CompareSession: sync mode, shared zoom/pan, and
    // per-cell transforms. Call after setImages() so the engine owns the frames
    // the transforms reference. The selection/ROI is applied via applyROI().
    void applySession(const mviewer::domain::CompareSession &s);

    // M15 P0#1: number of images currently loaded into the comparison. Used by
    // the crash-recovery autosave to decide whether a Compare session is active.
    int comparedImageCount() const { return m_engine.imageCount(); }

  signals:
    void syncToggled(bool on);
    // Hover pixel read from any cell, formatted for the status bar. Empty string clears.
    void pixelInfo(const QString &text);

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
    QCheckBox *m_syncZoomChk = nullptr;
    QCheckBox *m_syncDragChk = nullptr;
    bool m_syncZoom = true;
    bool m_syncDrag = true;
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
    bool isSplitOrSwipe() const
    {
        return (m_splitChk && m_splitChk->isChecked()) ||
               (m_swipeChk && m_swipeChk->isChecked());
    }

    // P0-4: split / swipe compare (only meaningful for exactly two images).
    QCheckBox *m_splitChk = nullptr;
    QCheckBox *m_swipeChk = nullptr;
    double m_splitPos = 0.5;
    bool m_splitDragging = false;
    void drawSplitCompare(QPainter &p);
    void drawSwipeCompare(QPainter &p, int x);
    void drawFitImage(QPainter &p, const QImage &img, const QRect &target);

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
    void refreshHistograms();

    // M16.1: cursor-sync crosshair (n/n) + focus-lock / reference pin (n/1).
    QCheckBox *m_crosshairChk = nullptr;   // 同步准星开关
    QPushButton *m_focusBtn = nullptr;      // 锁定/解除基准
    QLabel *m_focusLabel = nullptr;         // 显示当前基准格
    int m_focusIndex = -1;                  // 锁定的基准格索引 (-1 = 未锁定)
    int m_hoverIdx = -1;                     // 当前光标所在格 (用于锁定基准)
    int m_lastInspectX = -1;                 // 最近检视位置 (焦点切换时重刷)
    int m_lastInspectY = -1;
    void onCrosshairMoved(RawImageView *view, const QPointF &pos);
    void onFocusRequested(int cellIndex);
    int diffBaseIndex() const { return m_focusIndex >= 0 ? m_focusIndex : 0; }

    // Paints the most recent async diff result (from the EventBus) onto the
    // matching cell. Called on the UI thread via QueuedConnection.
    void refreshDiffOverlay();

    // EventBus subscription id for "CompareEngine.DiffResult"; unsubscribed in
    // the destructor because the EventBus is a process-global singleton and a
    // live subscription into a destroyed widget would crash.
    int m_diffSubId = 0;

    // ── M16.2: per-cell image adjustments ──
    struct CellAdjust
    {
        int brightness = 0;     // [-255, 255]
        float contrast = 1.0f;  // [0, 3.0]
        float gamma = 1.0f;     // [0.05, 8.0]
        float rGain = 1.0f;     // WB red gain [0.01, 5.0]
        float bGain = 1.0f;     // WB blue gain [0.01, 5.0]
        int rotation = 0;       // 0, 90, 180, 270
        bool hasCrop = false;
        int cropX = 0, cropY = 0, cropW = 0, cropH = 0;

        bool isIdentity() const {
            return brightness == 0 && std::abs(contrast - 1.0f) < 1e-6f &&
                   std::abs(gamma - 1.0f) < 1e-6f &&
                   std::abs(rGain - 1.0f) < 1e-6f &&
                   std::abs(bGain - 1.0f) < 1e-6f && rotation == 0 && !hasCrop;
        }
    };
    std::vector<CellAdjust> m_cellAdjusts;              // per-cell adjustment state
    int m_editIdx = -1;                                  // currently selected cell for editing
    ImageData applyAdjusts(const ImageData &src, const CellAdjust &a);

    // Edit panel widgets (inside side panel)
    QWidget *m_editPanel = nullptr;
    QLabel *m_editLabel = nullptr;                       // shows which cell is being edited
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
    void updateMetrics();

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
};