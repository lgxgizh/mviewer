#pragma once

#include <string>

namespace mviewer::core
{

// P5: crash diagnostics.
//
// Path of the next crash report (a .dmp minidump plus a sibling .txt log).
// Pure: does not create files. Testable.
std::string crashReportPath();

// Stable user-facing location for crash diagnostics. The directory is created
// when installCrashHandler() is called, not from inside the crash filter.
std::string crashReportDirectory();

// Install the unhandled-exception handler.
//
// On Windows, writes a minidump + a short .txt log into the crash-reports
// directory under %APPDATA%/MViewer/crash-reports/ on every crash. The handler
// is always active — no environment variable required for normal operation.
// No-op on platforms without a supported minidump backend.
void installCrashHandler(const std::string &appName = "MViewer");

} // namespace mviewer::core
