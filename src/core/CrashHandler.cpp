#include "core/CrashHandler.h"

#include "runtime_storage.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>

#include <dbghelp.h>
#pragma comment(lib, "DbgHelp.lib")
#endif

namespace mviewer::core
{

static std::string g_appName = "MViewer";

static QString crashDir()
{
    // Prefer the platform app-data location so crash reports survive temp cleanup
    // and are easy for users to find when filing a bug report.
    const QString base = mviewer::runtime::writableDirectory(QStandardPaths::AppDataLocation);
    return base.isEmpty() ? QString() : QDir(base).filePath(QStringLiteral("crash-reports"));
}

std::string crashReportPath()
{
    const QString dir = crashDir();
    if (dir.isEmpty())
        return {};
    const QString base = dir + "/" + QString::fromStdString(g_appName) + "-" +
                         QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    return (base + ".dmp").toUtf8().toStdString();
}

#ifdef Q_OS_WIN

static QString g_crashDir;

static LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS *ep)
{
    if (g_crashDir.isEmpty())
        return EXCEPTION_EXECUTE_HANDLER;
    QDir().mkpath(g_crashDir);

    const QString base = g_crashDir + "/" + QString::fromStdString(g_appName) + "-" +
                         QDateTime::currentDateTime().toString("yyyyMMdd-hhmmss");
    const std::wstring dmpW = (base + ".dmp").toStdWString();
    const std::wstring txtW = (base + ".txt").toStdWString();

    FILE *tf = _wfopen(txtW.c_str(), L"w");
    if (tf)
    {
        const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        fprintf(tf, "MViewer crash report\n");
        fprintf(tf, "exception_code=0x%08lX\n", code);
        fprintf(tf, "minidump=%ls\n", dmpW.c_str());
        fprintf(tf, "generated=%ls\n",
                QDateTime::currentDateTime().toString(Qt::ISODate).toStdWString().c_str());
        fclose(tf);
    }

    const HANDLE hFile = CreateFileW(dmpW.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        MINIDUMP_EXCEPTION_INFORMATION info{};
        info.ThreadId = GetCurrentThreadId();
        info.ExceptionPointers = ep;
        info.ClientPointers = FALSE;
        MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile, MiniDumpNormal,
                          ep ? &info : nullptr, nullptr, nullptr);
        CloseHandle(hFile);
    }
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // Q_OS_WIN

void installCrashHandler(const std::string &appName)
{
    g_appName = appName;

#ifndef Q_OS_WIN
    // Only the Windows minidump backend is supported. Other platforms simply
    // keep the default OS crash behaviour.
    return;
#else
    // Always install — crash dumps are written to AppData so they never pollute
    // the working directory or the test suite's temp tree. Tests that need to
    // avoid the handler can still call this safely (idempotent install).
    g_crashDir = crashDir();
    SetUnhandledExceptionFilter(crashExceptionFilter);
#endif
}

} // namespace mviewer::core
