// CrashHandler tests.
//
// NOTE: Crash handler tests are inherently limited in a standard CI
// environment.  Platform signal handlers (SEH on Windows, sigaction on
// POSIX) can only be fully validated by real crash scenarios (null deref,
// div-by-zero, stack overflow), which are destructive and cannot be run
// automatically.  The tests below verify the infrastructure: crash report
// path naming and handler installation safety.
//
// For full crash handling validation, manual testing is required:
//   1. Trigger a crash → verify .dmp is written under AppData/crash-reports/
//   2. Verify the .dmp opens in WinDbg with readable stack frames

#include "core/CrashHandler.h"

#include <QCoreApplication>
#include <QDir>
#include <QElapsedTimer>
#include <QFileInfo>
#include <QProcess>
#include <QSet>
#include <QThread>

#include <cstdio>
#include <string>

#ifdef Q_OS_WIN
#include <windows.h>
#endif

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            ++g_pass;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            ++g_fail;                                                                              \
        }                                                                                          \
    } while (0)

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    app.setApplicationName("MViewer");
    app.setOrganizationName("MViewer");

    for (int i = 1; i < argc; ++i)
    {
        if (QString::fromLocal8Bit(argv[i]) != QStringLiteral("--child-crash"))
            continue;
        mviewer::core::installCrashHandler("MViewer");
#ifdef Q_OS_WIN
        RaiseException(EXCEPTION_ACCESS_VIOLATION, 0, 0, nullptr);
#endif
        return 2;
    }

    // crashReportPath() is well-formed (ends in .dmp, lives under crash-reports).
    // Does not create any files.
    const std::string path = mviewer::core::crashReportPath();
    CHECK(!path.empty(), "crashReportPath() non-empty");
    CHECK(path.size() >= 4 && path.compare(path.size() - 4, 4, ".dmp") == 0,
          "crashReportPath() ends in .dmp");
    CHECK(path.find("crash-reports") != std::string::npos,
          "crashReportPath() under crash-reports dir");

    // Installing the handler must be safe to call repeatedly (idempotent).
    mviewer::core::installCrashHandler("MViewer");
    mviewer::core::installCrashHandler("MViewer");
    CHECK(mviewer::core::crashReportPath() == path,
          "installCrashHandler() is idempotent and preserves the crash path contract");

    const QString reportDir = QString::fromStdString(mviewer::core::crashReportDirectory());
    CHECK(reportDir.endsWith(QStringLiteral("crash-reports")),
          "crash reports use the AppData crash-reports directory");

#ifdef Q_OS_WIN
    const QDir reports(reportDir);
    const QSet<QString> before = [&reports]
    {
        QSet<QString> names;
        for (const QFileInfo &file : reports.entryInfoList(
                 {QStringLiteral("MViewer-*.dmp"), QStringLiteral("MViewer-*.txt")},
                 QDir::Files))
            names.insert(file.fileName());
        return names;
    }();
    QProcess child;
    child.setProgram(QCoreApplication::applicationFilePath());
    child.setArguments({QStringLiteral("--child-crash")});
    child.start();
    CHECK(child.waitForStarted(5000), "crash child process starts");
    CHECK(child.waitForFinished(15000), "crash child process terminates after SEH");
    CHECK(child.exitStatus() == QProcess::CrashExit || child.exitCode() != 0,
          "crash child exits abnormally after raising SEH");

    QSet<QString> created;
    QElapsedTimer wait;
    wait.start();
    while (wait.elapsed() < 5000)
    {
        for (const QFileInfo &file : reports.entryInfoList(
                 {QStringLiteral("MViewer-*.dmp"), QStringLiteral("MViewer-*.txt")},
                 QDir::Files))
        {
            if (!before.contains(file.fileName()))
                created.insert(file.fileName());
        }
        if (created.size() >= 2)
            break;
        QThread::msleep(50);
    }
    CHECK(created.size() >= 2, "real child crash produces a new dump and text report");
    bool hasDump = false;
    bool hasText = false;
    for (const QString &name : created)
    {
        hasDump = hasDump || name.endsWith(QStringLiteral(".dmp"));
        hasText = hasText || name.endsWith(QStringLiteral(".txt"));
        QFile::remove(reports.filePath(name));
    }
    CHECK(hasDump && hasText, "crash report pair has .dmp and sibling .txt files");
#else
    std::printf("  SKIP: real child SEH crash qualification is Windows-only\n");
#endif

    printf("\n==== CrashHandler test: %d passed, %d failed ====\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
