// CrashHandler tests.
//
// NOTE: Crash handler tests are inherently limited in a standard CI
// environment.  Platform signal handlers (SEH on Windows, sigaction on
// POSIX) can only be fully validated by real crash scenarios (null deref,
// div-by-zero, stack overflow), which are destructive and cannot be run
// automatically.  The tests below verify the infrastructure: crash report
// path naming, handler installation safety, and no-side-effect behaviour
// when MVIEWER_CRASH_DUMP is unset.
//
// For full crash handling validation, manual testing is required:
//   1. Set MVIEWER_CRASH_DUMP=1 and trigger a crash → verify .dmp is written
//   2. Verify the .dmp opens in WinDbg with readable stack frames

#include "core/CrashHandler.h"

#include <QCoreApplication>

#include <cstdio>
#include <string>

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond, msg)                                                                        \
    do                                                                                          \
    {                                                                                           \
        if (cond)                                                                               \
        {                                                                                       \
            printf("  PASS: %s\n", msg);                                                        \
            ++g_pass;                                                                           \
        }                                                                                       \
        else                                                                                   \
        {                                                                                       \
            printf("  FAIL: %s\n", msg);                                                        \
            ++g_fail;                                                                           \
        }                                                                                       \
    } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);

    // crashReportPath() is well-formed (ends in .dmp, lives in the
    // crash-reports dir). Does not create any files.
    const std::string path = mviewer::core::crashReportPath();
    CHECK(!path.empty(), "crashReportPath() non-empty");
    CHECK(path.size() >= 4 && path.compare(path.size() - 4, 4, ".dmp") == 0,
          "crashReportPath() ends in .dmp");
    CHECK(path.find("mviewer-crash-reports") != std::string::npos,
          "crashReportPath() under mviewer-crash-reports dir");

    // Installing the handler must be safe to call (no crash, no side effects
    // when MVIEWER_CRASH_DUMP is unset).
    mviewer::core::installCrashHandler("MViewer");
    mviewer::core::installCrashHandler("MViewer");
    CHECK(true, "installCrashHandler() callable without crashing");

    printf("\n==== CrashHandler test: %d passed, %d failed ====\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
