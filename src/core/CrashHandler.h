#pragma once

#include <string>

namespace mviewer::core
{

// P5: crash diagnostics.
//
// Path of the next crash report (a .dmp minidump plus a sibling .txt log).
// Pure: does not create files. Testable.
std::string crashReportPath();

// Install the unhandled-exception handler.
//
// On Windows, when the MVIEWER_CRASH_DUMP environment variable is set, a crash
// writes a minidump + a short .txt log into the crash-reports directory. The
// handler is opt-in so normal runs and the test suite are never affected.
// No-op on platforms without a supported minidump backend.
void installCrashHandler(const std::string &appName = "mviewer");

} // namespace mviewer::core
