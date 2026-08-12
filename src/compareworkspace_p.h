// M20 P0#2: shared private header for the CompareWorkspace implementation,
// which is split across several translation units by responsibility:
//   compareworkspace.cpp           core (cells, layout, session, loading)
//   compareworkspace_render.cpp    paint / diff overlay / blink / histograms
//   compareworkspace_editpanel.cpp edit panel, adjustments, presets, metrics
//   compareworkspace_interact.cpp  keyboard / mouse / pixel-link interaction
// Only CompareWorkspace TUs may include this header.
#pragma once

#include "compareworkspace.h"
#include "selectionmodel.h"
#include "widgets/histogramwidget.h"
#include "widgets/rawimageview.h"

#include <QPointer>

#include "core/EventBus.h"
#include "core/compare/DifferenceEngine.h"
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageRepository.h"
#include "core/image/QtConvert.h"

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
