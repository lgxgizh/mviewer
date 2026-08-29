// M20 P0#1: shared private header for the MainWindow implementation, which is
// split across several translation units by responsibility:
//   mainwindow.cpp            core wiring (models, browsing, image open)
//   mainwindow_ui.cpp         setupUi (widget/menu/dock construction)
//   mainwindow_commands.cpp   command registration + keyboard dispatch
//   mainwindow_navigation.cpp history / recent / favorites / navigation
//   mainwindow_session.cpp    workspace / project / restore / close persistence
//   mainwindow_session_notifications.cpp  update / crash notifications
//   mainwindow_export.cpp     report + image export
//   mainwindow_view.cpp       drag&drop, overlays, fullscreen, slideshow
// Only MainWindow TUs may include this header.
#pragma once

#include "mainwindow.h"

#include "MViewerVersion.h" // M24 version SSOT (generated from CMake project VERSION)

#include "appstate.h"
#include "core/RatingStore.h"
#include "core/SettingsIO.h"
#include "core/SidecarStore.h"
#include "core/analysis/ReportHtml.h"
#include "core/analyzer/Analyzer.h"
#include "core/cache/CacheManager.h"
#include "core/command/CallbackCommand.h"
#include "core/command/CompareCommand.h"
#include "core/command/DeleteCommand.h"
#include "core/command/OpenDirectoryCommand.h"
#include "core/command/RenameCommand.h"
#include "core/command/ToggleHistogramCommand.h"
#include "core/export/ExportManager.h"
#include "core/export/ExportJob.h"
#include "core/image/ImageFormats.h"
#include "core/image/ImageRepository.h"
#include "core/image/MetadataReader.h"
#include "core/image/QtConvert.h"
#include "core/image/RawMetadata.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/perf/MemoryTracker.h"
#include "core/project/ProjectSerializer.h"
#include "core/workspace/WorkspaceSerializer.h"

#include "analysisoverlaydialog.h"
#include "analysispanel.h"
#include "analyzermodel.h"
#include "batchdialog.h"
#include "breadcrumbbar.h"
#include "compareworkspace.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/compare/Histogram.h"
#include "core/render/Viewport.h"
#include "directorymodel.h"
#include "directorymonitor.h"
#include "directorytree.h"
#include "exportcommand.h"
#include "exportdialog.h"
#include "imagelistmodel.h"
#include "imageviewer.h"
#include "metadataoverlay.h"
#include "metadatapanel.h"
#include "pluginsettings.h"
#include "preferencesdialog.h"
#include "previewpanel.h"
#include "searchpanel.h"
#include "selectionmodel.h"
#include "thumbnailpanel.h"
#include "workspacemodel.h"

#include "core/update/UpdateChecker.h"
#include <QApplication>
#include <QBuffer>
#include <QCheckBox>
#include <QClipboard>
#include <QCloseEvent>
#include <QComboBox>
#include <QDateTime>
#include <QDesktopServices>
#include <QDialog>
#include <QDialogButtonBox>
#include <QDir>
#include <QDragEnterEvent>
#include <QDropEvent>
#include <QFile>
#include <QFileDialog>
#include <QFileInfo>
#include <QGuiApplication>
#include <QImage>
#include <QInputDialog>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QLabel>
#include <QLineEdit>
#include <QMenuBar>
#include <QMessageBox>
#include <QMetaObject>
#include <QMimeData>
#include <QMoveEvent>
#include <QPainter>
#include <QPointer>
#include <QPushButton>
#include <QProgressDialog>
#include <QResizeEvent>
#include <QScreen>
#include <QScrollBar>
#include <QSet>
#include <QSettings>
#include <QSplitter>
#include <QStandardPaths>
#include <QStatusBar>
#include <QTextBrowser>
#include <QTimer>
#include <QUrl>
#include <QUuid>
#include <QVBoxLayout>
#include <QWidget>
#include <thread>

#include <algorithm>
#include <optional>

// M15: decode a persisted compare-session JSON string into a value, or nullopt.
inline std::optional<mviewer::domain::CompareSession> decodeCompareSession(const std::string &json)
{
    if (json.empty())
        return std::nullopt;
    return mviewer::core::deserializeCompareSession(json);
}
