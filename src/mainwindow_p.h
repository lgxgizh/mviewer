// M20 P0#1: shared private header for the MainWindow implementation, which is
// split across several translation units by responsibility. Line counts are
// physical lines and the budget is enforced by scripts/complexity_gate.ps1
// (mainwindow.cpp fails above 1000; every mainwindow_*.cpp warns above 800):
//   mainwindow.cpp                        core wiring (models, browse, open)   860
//   mainwindow_ui.cpp                     setupUi dispatcher / empty state     40
//   mainwindow_ui_layout.cpp              widget / dock / status layout        621
//   mainwindow_ui_menus.cpp               menu bar construction                362
//   mainwindow_ui_connections.cpp         signal wiring + slot bodies          689
//   mainwindow_commands.cpp               command registration + key dispatch  619
//   mainwindow_navigation.cpp             history / recent / favorites / nav   383
//   mainwindow_session.cpp                workspace / project save/open        628
//   mainwindow_session_recovery.cpp       session restore / recovery / close   365
//   mainwindow_session_notifications.cpp  update / crash notifications         113
//   mainwindow_export.cpp                 report + image export                331
//   mainwindow_view.cpp                   drag&drop / overlays / slideshow     635
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
#include "core/export/ExportJob.h"
#include "core/export/ExportManager.h"
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
#include <QProgressDialog>
#include <QPushButton>
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

// Caps for whole-file reads of persisted JSON state. A workspace/project file
// lists paths (a few MB at most) and recovery/recent state is smaller still, so
// an oversized file means the path was replaced with something else — reject it
// instead of reading it into memory.
inline constexpr qint64 kMaxPersistedStateBytes = 64LL * 1024 * 1024;
inline constexpr qint64 kMaxRecoveryStateBytes = 16LL * 1024 * 1024;

// Read a whole persisted state file within `maxBytes`. Returns false (filling
// `error` when provided) if the file is missing, unreadable or too large.
inline bool readBoundedStateFile(QFile &file, const QString &path, qint64 maxBytes, QByteArray &out,
                                 QString *error = nullptr)
{
    if (!file.exists() || !file.open(QIODevice::ReadOnly | QIODevice::Text))
    {
        if (error)
            *error = QStringLiteral("无法读取文件：%1").arg(path);
        return false;
    }
    if (file.size() > maxBytes)
    {
        if (error)
            *error = QStringLiteral("文件过大，无法作为状态文件读取：%1").arg(path);
        return false;
    }
    out = file.read(std::min<qint64>(file.size(), maxBytes));
    return true;
}
