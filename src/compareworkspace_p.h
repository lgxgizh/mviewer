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
//   compareworkspace_interact.cpp            canvas mouse / pixel-link        704
//   compareworkspace_keyboard.cpp            keyboard-first compare controls  245
//   compareworkspace_nav.cpp                 pair navigation, layout presets  757
//   compareworkspace_render.cpp              paint modes, canvas host         484
//   compareworkspace_render_canvas.cpp       blink controller, canvas paint   449
//   compareworkspace_render_diff.cpp         diff overlay batch + metrics     353
//   compareworkspace_render_materialization.cpp cell raster materialization   805
//   compareworkspace_roi.cpp                 ROI box, HUD, measurement export 589
// Only CompareWorkspace TUs may include this header.
#pragma once

#include "compareworkspace.h"
#include "selectionmodel.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

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
