#include "core/CrashHandler.h"

#include "runtime_storage.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
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
constexpr size_t kCrashDirChars = 4096;
constexpr size_t kCrashAppChars = 256;
static wchar_t g_crashDir[kCrashDirChars] = {};
static wchar_t g_crashAppName[kCrashAppChars] = {};
static LPTOP_LEVEL_EXCEPTION_FILTER g_previousFilter = nullptr;
static volatile LONG g_crashInProgress = 0;
static volatile LONG g_forceDumpFail = 0;

static LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS *ep);

static LONG chainPreviousCrashFilter(EXCEPTION_POINTERS *ep)
{
    const LPTOP_LEVEL_EXCEPTION_FILTER previous = g_previousFilter;
    if (previous != nullptr && previous != crashExceptionFilter)
        return previous(ep);
    return EXCEPTION_CONTINUE_SEARCH;
}

static LONG WINAPI crashExceptionFilter(EXCEPTION_POINTERS *ep)
{
    if (InterlockedCompareExchange(&g_crashInProgress, 1, 0) != 0)
    {
        TerminateProcess(GetCurrentProcess(), 0xC0000409u);
        return EXCEPTION_EXECUTE_HANDLER;
    }
    if (g_crashDir[0] == L'\0' || g_crashAppName[0] == L'\0')
        return chainPreviousCrashFilter(ep);

    SYSTEMTIME now{};
    GetLocalTime(&now);
    wchar_t base[4096]{};
    if (FAILED(StringCchPrintfW(
            base, ARRAYSIZE(base), L"%s\\%s-%04u%02u%02u-%02u%02u%02u-%lu-%lu", g_crashDir,
            g_crashAppName, static_cast<unsigned>(now.wYear), static_cast<unsigned>(now.wMonth),
            static_cast<unsigned>(now.wDay), static_cast<unsigned>(now.wHour),
            static_cast<unsigned>(now.wMinute), static_cast<unsigned>(now.wSecond),
            static_cast<unsigned long>(GetCurrentProcessId()),
            static_cast<unsigned long>(GetCurrentThreadId()))))
        return chainPreviousCrashFilter(ep);

    wchar_t dmpPath[4096]{};
    wchar_t txtPath[4096]{};
    if (FAILED(StringCchPrintfW(dmpPath, ARRAYSIZE(dmpPath), L"%s.dmp", base)) ||
        FAILED(StringCchPrintfW(txtPath, ARRAYSIZE(txtPath), L"%s.txt", base)))
        return chainPreviousCrashFilter(ep);

    const HANDLE textFile = CreateFileW(txtPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                        CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (textFile != INVALID_HANDLE_VALUE)
    {
        const DWORD code = ep && ep->ExceptionRecord ? ep->ExceptionRecord->ExceptionCode : 0;
        char report[512]{};
        DWORD reportBytes = 0;
        if (SUCCEEDED(StringCchPrintfA(report, ARRAYSIZE(report),
                                       "MViewer crash report\r\nexception_code=0x%08lX\r\n",
                                       static_cast<unsigned long>(code))))
        {
            reportBytes = static_cast<DWORD>(lstrlenA(report));
            WriteFile(textFile, report, reportBytes, &reportBytes, nullptr);
        }
        CloseHandle(textFile);
    }

    BOOL dumped = FALSE;
    const HANDLE hFile = CreateFileW(dmpPath, GENERIC_WRITE, FILE_SHARE_READ, nullptr,
                                     CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (hFile != INVALID_HANDLE_VALUE)
    {
        if (InterlockedCompareExchange(&g_forceDumpFail, 0, 0) == 0)
        {
            MINIDUMP_EXCEPTION_INFORMATION info{};
            info.ThreadId = GetCurrentThreadId();
            info.ExceptionPointers = ep;
            info.ClientPointers = FALSE;
            dumped = MiniDumpWriteDump(GetCurrentProcess(), GetCurrentProcessId(), hFile,
                                       MiniDumpNormal, ep ? &info : nullptr, nullptr, nullptr);
        }
        CloseHandle(hFile);
        if (!dumped)
            DeleteFileW(dmpPath);
    }
    // Claim the crash only when the dump is actually on disk. Otherwise WER
    // (or the filter we replaced) still gets the exception.
    if (!dumped)
        return chainPreviousCrashFilter(ep);
    return EXCEPTION_EXECUTE_HANDLER;
}

#endif // Q_OS_WIN

namespace
{

// Crash dumps are written one .dmp/.txt pair per crash and nothing ever removed
// them, so a crash loop (or months of use) grew the folder without limit. Keep
// the newest N reports and drop the rest at startup — before SEH is installed,
// so no handler can observe the folder changing.
constexpr int kMaxCrashReports = 20;

void pruneCrashReports(const QString &dir)
{
    QDir reports(dir);
    const QFileInfoList dumps =
        reports.entryInfoList({QStringLiteral("*.dmp")}, QDir::Files, QDir::Time);
    for (int i = kMaxCrashReports; i < dumps.size(); ++i)
    {
        const QFileInfo &dump = dumps.at(i);
        QFile::remove(dump.absoluteFilePath());
        QFile::remove(dump.absolutePath() + "/" + dump.completeBaseName() + ".txt");
    }
}

} // namespace

void setCrashDumpWriteFaultForTest(bool fail)
{
#ifdef Q_OS_WIN
    InterlockedExchange(&g_forceDumpFail, fail ? 1 : 0);
#else
    (void)fail;
#endif
}

long invokeCrashFilterForTest()
{
#ifdef Q_OS_WIN
    InterlockedExchange(&g_crashInProgress, 0);
    EXCEPTION_RECORD record{};
    record.ExceptionCode = EXCEPTION_ACCESS_VIOLATION;
    CONTEXT context{};
    EXCEPTION_POINTERS pointers{};
    pointers.ExceptionRecord = &record;
    pointers.ContextRecord = &context;
    return crashExceptionFilter(&pointers);
#else
    return 0;
#endif
}

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
    // and copy the path into fixed buffers BEFORE registering SEH: the handler
    // must not create directories, touch Qt, or walk a heap string after a
    // process has already entered an exceptional state.
    const QString dir = crashDir();
    g_crashDir[0] = L'\0';
    g_crashAppName[0] = L'\0';
    if (!dir.isEmpty())
    {
        QDir().mkpath(dir);
        pruneCrashReports(dir);
        const std::wstring dirW = dir.toStdWString();
        if (FAILED(StringCchCopyW(g_crashDir, kCrashDirChars, dirW.c_str())))
            g_crashDir[0] = L'\0';
    }
    const std::wstring appW = QString::fromStdString(g_appName).toStdWString();
    if (FAILED(StringCchCopyW(g_crashAppName, kCrashAppChars, appW.c_str())))
        g_crashAppName[0] = L'\0';
    InterlockedExchange(&g_crashInProgress, 0);
    const LPTOP_LEVEL_EXCEPTION_FILTER previous = SetUnhandledExceptionFilter(crashExceptionFilter);
    // Re-installing must not chain the filter to itself.
    if (previous != crashExceptionFilter)
        g_previousFilter = previous;
#endif
}

} // namespace mviewer::core
