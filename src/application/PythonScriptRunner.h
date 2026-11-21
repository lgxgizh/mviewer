#pragma once

// Python script bridge (report feature 9).
//
// A user-supplied .py script is run as a subprocess on the current image. The
// protocol is deliberately boring: the runner writes a JSON request to a temp
// file, the script receives `<script> <request.json> <response.json>`, and the
// runner reads the response back. No shell, no imports into this process, a hard
// timeout, and the script only ever runs because the user picked it and pressed
// Run.
//
// Request:  {"image": "<absolute path>", "args": {...}}
// Response: {"ok": true, "message": "...", "boxes": [[x, y, w, h, "label"], ...]}
//           {"ok": false, "message": "why"}

#include <QJsonObject>
#include <QString>
#include <QStringList>

#include <functional>

namespace mviewer::application
{

struct PythonScriptResult
{
    bool ok = false;
    QString message;
    QString responsePath;
    QStringList boxes; // "x,y,w,h,label" strings, as reported by the script
};

class PythonScriptRunner
{
  public:
    static PythonScriptRunner &instance();

    /// Interpreter preference order: $MVIEWER_PYTHON, then `python3`, `python`.
    /// An empty result means no interpreter was found.
    QString detectInterpreter();
    QString interpreter() const;
    void setInterpreter(const QString &path);

    /// Run `scriptPath` with `request`. Returns a result; `error` carries the
    /// transport-level failure (no interpreter, timeout, unreadable response).
    /// Blocks the calling thread for up to `timeoutMs` — UI code must use
    /// runAsync() instead.
    PythonScriptResult run(const QString &scriptPath, const QJsonObject &request, QString &error,
                           int timeoutMs = 30000);

    /// Async variant: the work happens on a worker thread and `completion` is
    /// delivered on `context`'s thread. Without this, a script that runs to its
    /// timeout froze the whole interface.
    using ScriptCompletion =
        std::function<void(const PythonScriptResult &result, const QString &error)>;
    void runAsync(const QString &scriptPath, const QJsonObject &request, QObject *context,
                  ScriptCompletion completion, int timeoutMs = 30000);

    /// True when the path looks like a Python script we are willing to run.
    static bool isScriptPath(const QString &path);

  private:
    PythonScriptRunner() = default;

    QString m_interpreter;
};

} // namespace mviewer::application
