#include "core/Logger.h"

#include "runtime_storage.h"

#include "MViewerVersion.h" // M24 version SSOT (generated from CMake project VERSION)

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QMutex>
#include <QStandardPaths>
#include <QtGlobal>

#include <cstdio>

namespace mviewer::core
{
namespace
{

QString g_logDir;
QString g_logPath;
QFile *g_logFile = nullptr;
QMutex g_logMtx;
bool g_installed = false;
bool g_handlerInstalled = false;
QtMessageHandler g_previousHandler = nullptr;

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
    const QByteArray bytes = line.toUtf8();
    const bool flushNow = type == QtWarningMsg || type == QtCriticalMsg || type == QtFatalMsg;

    // stderr and the file share one lock so concurrent threads cannot interleave
    // a line. Debug/info stay buffered; warning and above flush immediately.
    QMutexLocker lock(&g_logMtx);
    std::fprintf(stderr, "%s\n", bytes.constData());
    if (flushNow)
        std::fflush(stderr);
    if (g_logFile && g_logFile->isOpen())
    {
        g_logFile->write(bytes);
        g_logFile->write("\n", 1);
        if (flushNow)
            g_logFile->flush();
    }
}

} // anonymous namespace

void installFileLogger(const std::string &appName)
{
    if (g_installed)
        return;
    g_installed = true;

    // Prefer the platform app-data location; fall back to temp if unavailable.
    QString base = mviewer::runtime::writableDirectory(QStandardPaths::AppDataLocation);
    if (base.isEmpty())
        return;

    // AppDataLocation already includes the org/app name set on QCoreApplication,
    // but we still append "logs" so the directory layout is predictable.
    g_logDir = QDir(base).filePath(QStringLiteral("logs"));
    if (!QDir().mkpath(g_logDir) || !QFileInfo(g_logDir).isWritable())
        return;

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

    if (!g_handlerInstalled)
    {
        g_previousHandler = qInstallMessageHandler(messageHandler);
        g_handlerInstalled = true;
    }

    // Emit a startup banner so the log always has a clear session boundary.
    // M24: version comes from the CMake-generated header (single source).
    qInfo("=== %s %s session start (build %s) ===", appName.c_str(), MVIEWER_VERSION_STRING,
          MVIEWER_VERSION_FULL);
    qInfo("log file: %s", qPrintable(g_logPath));
}

std::string currentLogPath()
{
    return g_logPath.toUtf8().toStdString();
}

void shutdownFileLogger()
{
    if (!g_installed && !g_handlerInstalled)
        return;

    // Restore the previous handler before closing the file so a message that
    // arrives during shutdown is not written into a file we are deleting.
    // If install returned before replacing the handler, leave it alone.
    if (g_handlerInstalled)
    {
        qInstallMessageHandler(g_previousHandler);
        g_previousHandler = nullptr;
        g_handlerInstalled = false;
    }
    g_installed = false;

    QMutexLocker lock(&g_logMtx);
    std::fflush(stderr);
    if (g_logFile)
    {
        g_logFile->flush();
        g_logFile->close();
        delete g_logFile;
        g_logFile = nullptr;
    }
}

std::string logDirectory()
{
    return g_logDir.toUtf8().toStdString();
}

} // namespace mviewer::core
