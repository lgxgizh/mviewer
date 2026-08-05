#include "core/Logger.h"

#include "MViewerVersion.h" // M24 version SSOT (generated from CMake project VERSION)

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QTextStream>
#include <QtGlobal>

#include <cstdio>
#include <mutex>

namespace mviewer::core
{
namespace
{

QString g_logDir;
QString g_logPath;
QFile *g_logFile = nullptr;
QMutex g_logMtx;
bool g_installed = false;

QString levelName(QtMsgType type)
{
    switch (type)
    {
    case QtDebugMsg:
        return QStringLiteral("DEBUG");
    case QtInfoMsg:
        return QStringLiteral("INFO");
    case QtWarningMsg:
        return QStringLiteral("WARN");
    case QtCriticalMsg:
        return QStringLiteral("ERROR");
    case QtFatalMsg:
        return QStringLiteral("FATAL");
    }
    return QStringLiteral("????");
}

void messageHandler(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    const QString ts =
        QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz"));
    const QString level = levelName(type);

    // Build a single structured line:
    //   2026-07-24 12:34:56.789 [INFO ] file.cpp:42  message text
    QString line = QStringLiteral("%1 [%2] ").arg(ts, level.leftJustified(5));
    if (ctx.file && ctx.line > 0)
    {
        // Strip path prefix — keep only the basename for readability.
        const char *base = ctx.file;
        for (const char *p = ctx.file; *p; ++p)
            if (*p == '/' || *p == '\\')
                base = p + 1;
        line += QStringLiteral("%1:%2  ").arg(QString::fromUtf8(base)).arg(ctx.line);
    }
    line += msg;

    // Always mirror to stderr so console / CI runs stay useful.
    fprintf(stderr, "%s\n", qPrintable(line));
    fflush(stderr);

    // Append to the log file under a mutex so concurrent threads don't interleave.
    QMutexLocker lock(&g_logMtx);
    if (g_logFile && g_logFile->isOpen())
    {
        QTextStream out(g_logFile);
        out << line << '\n';
        out.flush();
    }
}

} // anonymous namespace

void installFileLogger(const std::string &appName)
{
    if (g_installed)
        return;
    g_installed = true;

    // Prefer the platform app-data location; fall back to temp if unavailable.
    QString base = QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        base = QDir::tempPath();

    // AppDataLocation already includes the org/app name set on QCoreApplication,
    // but we still append "logs" so the directory layout is predictable.
    g_logDir = base + QStringLiteral("/logs");
    QDir().mkpath(g_logDir);

    const QString date = QDateTime::currentDateTime().toString(QStringLiteral("yyyyMMdd"));
    g_logPath = g_logDir + QStringLiteral("/mviewer-") + date + QStringLiteral(".log");

    g_logFile = new QFile(g_logPath);
    // Append mode so multiple launches on the same day share one file.
    if (!g_logFile->open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text))
    {
        // Fall back to stderr-only if the file cannot be opened.
        delete g_logFile;
        g_logFile = nullptr;
    }

    qInstallMessageHandler(messageHandler);

    // Emit a startup banner so the log always has a clear session boundary.
    // M24: version comes from the CMake-generated header (single source).
    qInfo("=== %s %s session start (build %s) ===", appName.c_str(), MVIEWER_VERSION_STRING,
          MVIEWER_VERSION_FULL);
    qInfo("log file: %s", qPrintable(g_logPath));
}

std::string currentLogPath()
{
    return g_logPath.toStdString();
}

std::string logDirectory()
{
    return g_logDir.toStdString();
}

} // namespace mviewer::core
