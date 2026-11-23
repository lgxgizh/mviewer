#include "application/PythonScriptRunner.h"

#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QPointer>
#include <QProcess>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QThread>
#include <QThreadPool>
#include <QTimer>

#include <chrono>

namespace mviewer::application
{
namespace
{
constexpr int kDefaultTimeoutMs = 30000;

QString firstExecutable(const QStringList &candidates)
{
    for (const QString &candidate : candidates)
    {
        if (candidate.isEmpty())
            continue;
        const QString path = QFileInfo(candidate).isAbsolute()
                                 ? candidate
                                 : QStandardPaths::findExecutable(candidate);
        if (!path.isEmpty() && QFileInfo(path).isExecutable())
            return path;
    }
    return {};
}

/// Pull "x,y,w,h,label" strings out of the response's `boxes` array.
QStringList parseBoxes(const QJsonObject &response)
{
    QStringList out;
    const QJsonArray boxes = response.value(QStringLiteral("boxes")).toArray();
    for (const QJsonValue &value : boxes)
    {
        const QJsonArray item = value.toArray();
        if (item.size() < 4)
            continue;
        const QString label = item.size() >= 5 ? item.at(4).toString() : QString();
        out.append(QStringLiteral("%1,%2,%3,%4,%5")
                       .arg(item.at(0).toDouble())
                       .arg(item.at(1).toDouble())
                       .arg(item.at(2).toDouble())
                       .arg(item.at(3).toDouble())
                       .arg(label));
    }
    return out;
}
} // namespace

PythonScriptRunner &PythonScriptRunner::instance()
{
    static PythonScriptRunner runner;
    return runner;
}

bool PythonScriptRunner::isScriptPath(const QString &path)
{
    if (path.isEmpty())
        return false;
    const QFileInfo info(path);
    return info.isFile() && info.suffix().compare(QLatin1String("py"), Qt::CaseInsensitive) == 0;
}

QString PythonScriptRunner::detectInterpreter()
{
    {
        std::lock_guard<std::mutex> lock(m_mutex);
        if (!m_interpreter.isEmpty())
            return m_interpreter;
    }
    const QString configured = qEnvironmentVariable("MVIEWER_PYTHON");
    const QString found = firstExecutable(
        {configured, QStringLiteral("python3"), QStringLiteral("python"), QStringLiteral("py")});
    std::lock_guard<std::mutex> lock(m_mutex);
    // Another thread may have set it while we were probing; keep the first one.
    if (m_interpreter.isEmpty())
        m_interpreter = found;
    return m_interpreter;
}

QString PythonScriptRunner::interpreter() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_interpreter;
}

void PythonScriptRunner::setInterpreter(const QString &path)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_interpreter = path;
}

PythonScriptResult PythonScriptRunner::run(const QString &scriptPath, const QJsonObject &request,
                                           QString &error, int timeoutMs)
{
    PythonScriptResult result;
    // The out-parameter is reset on entry: callers legitimately reuse one
    // variable across attempts, and a previous failure must not leak into a
    // later success.
    error.clear();
    if (!isScriptPath(scriptPath))
    {
        error = QStringLiteral("not a Python script: %1").arg(scriptPath);
        return result;
    }
    const QString interpreter = detectInterpreter();
    if (interpreter.isEmpty())
    {
        error = QStringLiteral("no Python interpreter found (set MVIEWER_PYTHON)");
        return result;
    }

    QTemporaryDir work(QDir::tempPath() + QStringLiteral("/mviewer-py-XXXXXX"));
    if (!work.isValid())
    {
        error = QStringLiteral("no temporary directory available");
        return result;
    }
    const QString requestPath = work.filePath(QStringLiteral("request.json"));
    const QString responsePath = work.filePath(QStringLiteral("response.json"));
    QFile requestFile(requestPath);
    if (!requestFile.open(QIODevice::WriteOnly | QIODevice::Truncate))
    {
        error = QStringLiteral("could not write the script request");
        return result;
    }
    requestFile.write(QJsonDocument(request).toJson(QJsonDocument::Compact));
    requestFile.close();

    QProcess process;
    process.setProgram(interpreter);
    process.setArguments({scriptPath, requestPath, responsePath});
    // A script must never inherit a shell or a modified environment silently.
    process.setProcessChannelMode(QProcess::SeparateChannels);
    process.start();
    if (!process.waitForStarted(timeoutMs))
    {
        error = QStringLiteral("the script did not start");
        return result;
    }
    if (!process.waitForFinished(timeoutMs))
    {
        process.kill();
        process.waitForFinished(2000);
        error = QStringLiteral("the script exceeded the %1 ms timeout")
                    .arg(timeoutMs == kDefaultTimeoutMs ? 30000 : timeoutMs);
        return result;
    }
    if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0)
    {
        const QString stderrText = QString::fromUtf8(process.readAllStandardError()).trimmed();
        error = stderrText.isEmpty()
                    ? QStringLiteral("the script exited with code %1").arg(process.exitCode())
                    : stderrText;
        return result;
    }

    QFile responseFile(responsePath);
    if (!responseFile.open(QIODevice::ReadOnly))
    {
        error = QStringLiteral("the script did not write a response");
        return result;
    }
    const QJsonObject response = QJsonDocument::fromJson(responseFile.readAll()).object();
    responseFile.close();
    if (response.isEmpty())
    {
        error = QStringLiteral("the script response was not valid JSON");
        return result;
    }

    result.ok = response.value(QStringLiteral("ok")).toBool(true);
    result.message = response.value(QStringLiteral("message")).toString();
    result.boxes = parseBoxes(response);
    if (!result.ok && result.message.isEmpty())
        result.message = QStringLiteral("the script reported failure");
    return result;
}

void PythonScriptRunner::runAsync(const QString &scriptPath, const QJsonObject &request,
                                  QObject *context, ScriptCompletion completion, int timeoutMs)
{
    auto delivered = std::make_shared<ScriptCompletion>(std::move(completion));
    // `context` can die while the script is running (the user closes the
    // dialog). A raw pointer captured in the queued lambda is then a
    // use-after-free, and QMetaObject::invokeMethod would still target it.
    const QPointer<QObject> guard(context);
    // Routed through TaskScheduler rather than QThreadPool::globalInstance():
    // one bounded, instrumented pool instead of an unbounded global one.
    ::TaskScheduler::instance().submit(
        ::TaskScheduler::Priority::Background,
        [scriptPath, request, guard, delivered, timeoutMs](const ::TaskScheduler::TaskContext &)
        {
            if (guard.isNull())
                return; // the context is gone; nobody is listening
            QString error;
            const PythonScriptResult result =
                PythonScriptRunner::instance().run(scriptPath, request, error, timeoutMs);
            const QString errorCopy = error;
            // Back on the context's (UI) thread: the dialog must not be touched
            // from the worker. Re-check inside the lambda too — the object can
            // die between the check above and the queued delivery.
            QMetaObject::invokeMethod(
                guard.data(),
                [guard, delivered, result, errorCopy]()
                {
                    if (guard.isNull())
                        return;
                    (*delivered)(result, errorCopy);
                },
                Qt::QueuedConnection);
        });
}

} // namespace mviewer::application
