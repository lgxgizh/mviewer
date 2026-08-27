#include "core/CrashHandler.h"

#include "runtime_storage.h"

#include <QDateTime>
#include <QDir>
#include <QStandardPaths>

#include <cstdio>

#ifdef Q_OS_WIN
#include <windows.h>

#include <dbghelp.h>
#include <strsafe.h>
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

std::string crashReportDirectory()
{
    return crashDir().toUtf8().toStdString();
}

#ifdef Q_OS_WIN

// All data read by the exception filter is prepared before the handler is
// installed. The filter itself uses fixed buffers and Win32 file APIs only;
// Qt, QString, QDir, and heap-owning formatting stay out of the crash path.
static std::wstring g_crashDir;
static std::wstring g_crashAppName;
static volatile LONG g_crashInProgress = 0;

static LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS *ep)
{
    if (InterlockedCompareExchange(&g_crashInProgress, 1, 0) != 0)
    {
        TerminateProcess(GetCurrentProcess(), 0xC0000409u);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (g_crashDir.empty() || g_crashAppName.empty())
        return EXCEPTION_EXECUTE_HANDLER;

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t base[4096]{};
    if (FAILED(StringCchPrintfW(base,
                                ARRAYSIZE(base),
                                L"%s\\%s-%04u%02u%02u-%02u%02u%02u-%lu-%lu",
                                g_crashDir.c_str(),
                                g_crashAppName.c_str(),
                                static_cast<unsigned>(now.wYear),
                                static_cast<unsigned>(now.wMonth),
                                static_cast<unsigned>(now.wDay),
                                static_cast<unsigned>(now.wHour),
                                static_cast<unsigned>(now.wMinute),
                                static_cast<unsigned>(now.wSecond),
                                static_cast<unsigned long>(GetCurrentProcessId()),
                                static_cast<unsigned long>(GetCurrentThreadId()))))
        return EXCEPTION_EXECUTE_HANDLER;

    wchar_t dmpPath[4096]{};
    wchar_t txtPath[4096]{};
    if (FAILED(StringCchPrintfW(dmpPath, ARRAYSIZE(dmpPath), L"%s.dmp", base)) ||
        FAILED(StringCchPrintfW(txtPath, ARRAYSIZE(txtPath), L"%s.txt", base)))
        return EXCEPTION_EXECUTE_HANDLER;

    const HANDLE textFile = CreateFileW(txtPath,
                                        GENERIC_WRITE,
                                        FILE_SHARE_READ,
                                        nullptr,
                                        CREATE_ALWAYS,
                                        FILE_ATTRIBUTE_NORMAL,
                                        nullptr);
    if (textFile != INVALID_HANDLE_VALUE)
    {
        const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        char report[512]{};
        DWORD reportBytes = 0;
        if (SUCCEEDED(StringCchPrintfA(report,
                                       ARRAYSIZE(report),
                                       "MViewer crash report\r\nexception_code=0x%08lX\r\n",
                                       static_cast<unsigned long>(code))))
        {
            reportBytes = static_cast<DWORD>(lstrlenA(report));
            WriteFile(textFile, report, reportBytes, &reportBytes, nullptr);
        }
        CloseHandle(textFile);
    }

    const HANDLE hFile = CreateFileW(dmpPath,
                                     GENERIC_WRITE,
                                     FILE_SHARE_READ,
                                     nullptr,
                                     CREATE_ALWAYS,
                                     FILE_ATTRIBUTE_NORMAL,
                                     nullptr);
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
    // the working directory or the test suite's temp tree. Prepare the folder
    // before registering SEH: the handler must not create directories or use
    // Qt after a process has already entered an exceptional state.
    const QString dir = crashDir();
    g_crashDir = dir.toStdWString();
    g_crashAppName = QString::fromStdString(g_appName).toStdWString();
    if (!dir.isEmpty())
        QDir().mkpath(dir);
    InterlockedExchange(&g_crashInProgress, 0);
    SetUnhandledExceptionFilter(crashExceptionFilter);
#endif
}

} // namespace mviewer::core
