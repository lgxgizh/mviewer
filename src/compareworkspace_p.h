// M20 P0#2: shared private header for the CompareWorkspace implementation,
// which is split across several translation units by responsibility. Line counts
// are physical lines and the budget is enforced by scripts/complexity_gate.ps1
// (compareworkspace.cpp fails above 800; every compareworkspace_*.cpp warns
// above 800 and fails above 1000):
//   compareworkspace.cpp                     core (cells, layout, session, load) 770
//   compareworkspace_analysis.cpp            histograms / metrics panels      642
//   compareworkspace_controls.cpp            toolbar + mode controls          565
//   compareworkspace_display_planner.cpp     fit/LOD planning (free functions) 174
//   compareworkspace_editpanel.cpp           edit panel, adjustments, presets 650
//   compareworkspace_adjust_export.cpp       per-cell USM controls + full-res export
//   compareworkspace_interact.cpp            canvas mouse / pixel-link        704
//   compareworkspace_keyboard.cpp            keyboard-first compare controls  245
//   compareworkspace_nav.cpp                 pair navigation, layout presets  757
//   compareworkspace_render.cpp              paint modes, canvas host         484
//   compareworkspace_render_canvas.cpp       blink controller, canvas paint   449
//   compareworkspace_render_canvas_paint.cpp canvas paint, pixel grid, overlays 168
//   compareworkspace_render_diff.cpp         diff overlay batch + metrics     353
//   compareworkspace_render_materialization.cpp cell raster materialization   647
//   compareworkspace_roi.cpp                 ROI box, HUD, measurement export 589
// Only CompareWorkspace TUs may include this header.
#pragma once

#include "compare_diff_renderer.h"
#include "compare_display_manager.h"
#include "compare_roi_controller.h"
#include "compare_session_state.h"
#include "compareworkspace.h"
#include "compareworkspace_composite_gl.h"
#include "compareworkspace_adjusted_export.h"
#include "selectionmodel.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QApplication>
#include <QPointer>

#include "core/compare/DifferenceEngine.h"
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"
#include "core/render/RenderEngine.h"

#include <QCheckBox>
#include <QComboBox>
#include <QDialog>
#include <QDir>

#include <QFileDialog>
#include <QGridLayout>
#include <QHBoxLayout>
#include <QHeaderView>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QKeyEvent>
#include <QLabel>
#include <QMessageBox>
#include <QMetaObject>
#include <QMouseEvent>
#include <QPainter>
#include <QProgressBar>
#include <QScrollArea>
#include <QSignalBlocker>
#include <QStackedLayout>
#include <QStandardPaths>
#include <QTableWidget>
#include <QTimer>
#include <QVBoxLayout>
#include <QWheelEvent>
#include <algorithm>
#include <vector>

// RGB materialization under Qt's ~256 MB limit (see setImages probe).
constexpr qint64 kCompareAnalysisFeasiblePixels = 60 * 1000 * 1000; // 60 MP

// Passive UI state bundle (pimpl-style `d`).
//
// Every panel widget pointer used by the implementation. They are plain
// handles owned by CompareWorkspace; keeping them here keeps
// compareworkspace.h far below its line ceiling and lets the header drop
// the QWidget-heavy declarations from its private section.
//
// Deliberately NOT an object-oriented controller: the paint/UI TUs read these
// fields directly, so no callback boundary is introduced around them.
struct CompareWorkspace::Private
{
    QCheckBox *syncZoomChk = nullptr;
    QCheckBox *syncDragChk = nullptr;
    QCheckBox *syncRotateChk = nullptr;
    QCheckBox *syncRotatePanelChk = nullptr;
    QCheckBox *uniformScaleChk = nullptr;
    QWidget *grid = nullptr;
    QGridLayout *layout = nullptr;
    QWidget *compareGridPage = nullptr;
    QWidget *compareLoadingPage = nullptr;
    QLabel *compareLoadingLabel = nullptr;
    QProgressBar *compareLoadingProgress = nullptr;
    QStackedLayout *pageStack = nullptr;
    QCheckBox *blinkChk = nullptr;
    QTimer *blinkTimer = nullptr;
    QPushButton *temporaryCompareButton = nullptr;
    QCheckBox *splitChk = nullptr;
    QCheckBox *overlayChk = nullptr;
    QSlider *overlayAlphaSlider = nullptr;
    QLabel *overlayAlphaLabel = nullptr;
    QCheckBox *checkerChk = nullptr;
    QSlider *checkerSizeSlider = nullptr;
    QLabel *checkerSizeLabel = nullptr;
    QCheckBox *pixelLinkChk = nullptr;
    QPushButton *clearLinksBtn = nullptr;
    QLabel *linkInfoLabel = nullptr;
    QCheckBox *diffHighlightChk = nullptr;
    QCheckBox *diffOverlayChk = nullptr;
    QComboBox *diffGainCombo = nullptr;
    QSpinBox *gridColsSpin = nullptr;
    QLabel *layoutStatusLabel = nullptr;
    QSlider *thresholdSlider = nullptr;
    QLabel *thresholdLabel = nullptr;
    QPushButton *autoThresholdBtn = nullptr;
    QComboBox *layoutCombo = nullptr;
    QComboBox *channelCombo = nullptr;
    QCheckBox *sideChk = nullptr;
    QWidget *sidePanel = nullptr;
    QTableWidget *inspector = nullptr;
    QComboBox *csCombo = nullptr;
    QComboBox *kernelCombo = nullptr;
    QLabel *coordLabel = nullptr;
    QLabel *statsLabel = nullptr;
    QLabel *histTitle = nullptr;
    QCheckBox *histRChk = nullptr;
    QCheckBox *histGChk = nullptr;
    QCheckBox *histBChk = nullptr;
    QCheckBox *histLumaChk = nullptr;
    QCheckBox *histVChk = nullptr;
    QCheckBox *histLogChk = nullptr;
    QCheckBox *roiHistChk = nullptr;
    QLabel *roiStatusLabel = nullptr;
    QLabel *roiGeometryLabel = nullptr;
    QLabel *roiDeltaLabel = nullptr;
    QTableWidget *roiTable = nullptr;
    QPushButton *clearRoiBtn = nullptr;
    QPushButton *copyRoiBtn = nullptr;
    QPushButton *roiHud = nullptr;
    QCheckBox *crosshairChk = nullptr;
    QPushButton *focusBtn = nullptr;
    QLabel *focusLabel = nullptr;
    QLabel *frameLabel = nullptr;
    QSpinBox *frameSpin = nullptr;
    QWidget *editPanel = nullptr;
    QLabel *editLabel = nullptr;
    QSlider *brightSlider = nullptr;
    QSlider *contrastSlider = nullptr;
    QSlider *gammaSlider = nullptr;
    QSlider *rGainSlider = nullptr;
    QSlider *bGainSlider = nullptr;
    QLabel *brightVal = nullptr;
    QLabel *contrastVal = nullptr;
    QLabel *gammaVal = nullptr;
    QLabel *rGainVal = nullptr;
    QLabel *bGainVal = nullptr;
    QLabel *rotVal = nullptr;
    QPushButton *resetAdjBtn = nullptr;
    QCheckBox *usmChk = nullptr;
    QCheckBox *usmLumaChk = nullptr;
    QSlider *usmAmountSlider = nullptr;
    QSlider *usmRadiusSlider = nullptr;
    QSlider *usmThresholdSlider = nullptr;
    QLabel *usmAmountVal = nullptr;
    QLabel *usmRadiusVal = nullptr;
    QLabel *usmThresholdVal = nullptr;
    QPushButton *exportAdjustedBtn = nullptr;
    QLabel *metricLabel = nullptr;
    QCheckBox *paneHistOverlayChk = nullptr;
    QCheckBox *filenameOverlayChk = nullptr;
    QCheckBox *pixelGridChk = nullptr;
    QCheckBox *perPaneHistChk = nullptr;
    QPushButton *savePresetBtn = nullptr;
    QPushButton *loadPresetBtn = nullptr;
    QPushButton *swapBtn = nullptr;
    QPushButton *analyzeBtn = nullptr;
    QPushButton *exportReportBtn = nullptr;
    QPushButton *exitBtn = nullptr;
    QCheckBox *autoAlignChk = nullptr;
    QLabel *compareStatusLabel = nullptr;
    QDialog *shortcutHelpDialog = nullptr;
};
