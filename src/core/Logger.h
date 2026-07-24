#pragma once

#include <string>

namespace mviewer::core
{

// Lightweight structured logging for MViewer.
//
// Installs a Qt message handler that writes every qDebug/qInfo/qWarning/
// qCritical/qFatal call to a rotating log file under the application data
// directory. Also mirrors messages to stderr so console runs stay useful.
//
// Usage (once at process start, after QCoreApplication is constructed):
//   mviewer::core::installFileLogger("MViewer");
//
// The log file path is:
//   %APPDATA%/MViewer/logs/mviewer-YYYYMMDD.log   (Windows)
//   ~/.local/share/MViewer/logs/mviewer-YYYYMMDD.log  (Linux)
//
// Header is intentionally thin; the .cpp may use Qt.

// Install the file logger. Safe to call multiple times (idempotent).
// `appName` is used as the log directory name under the app-data root.
void installFileLogger(const std::string &appName = "MViewer");

// Return the absolute path of today's log file (empty if logger not installed).
std::string currentLogPath();

// Return the directory that holds log files.
std::string logDirectory();

} // namespace mviewer::core
