// Python script bridge (report feature 9): transport contract and guards.
//
// The interpreter-dependent case is skipped (not failed) when no Python is
// available, so the suite is meaningful on a bare Windows box and still
// end-to-end on CI.
#include "application/PythonScriptRunner.h"

#include <QApplication>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonObject>
#include <QStandardPaths>
#include <QTemporaryDir>

#include <iostream>

namespace
{
int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::cout << "[ok] " << msg << "\n";                                                   \
        else                                                                                       \
        {                                                                                          \
            std::cout << "[FAIL] " << msg << " (" << __FILE__ << ":" << __LINE__ << ")\n";         \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)
} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    setbuf(stdout, nullptr);
    using mviewer::application::PythonScriptRunner;

    QTemporaryDir typeTmp;
    CHECK(typeTmp.isValid(), "type fixture dir created");
    const QString pyFile = typeTmp.filePath(QStringLiteral("algo.py"));
    const QString txtFile = typeTmp.filePath(QStringLiteral("algo.txt"));
    {
        QFile f(pyFile);
        f.open(QIODevice::WriteOnly);
        f.write("pass\n");
    }
    {
        QFile f(txtFile);
        f.open(QIODevice::WriteOnly);
        f.write("x\n");
    }

    CHECK(PythonScriptRunner::isScriptPath(pyFile), "a .py path is a script");
    CHECK(!PythonScriptRunner::isScriptPath(txtFile), "a non-Python file is not a script");
    CHECK(!PythonScriptRunner::isScriptPath(QString()), "an empty path is not a script");

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    if (!tmp.isValid())
        return 1;

    auto &runner = PythonScriptRunner::instance();
    QString error;
    const QString notAScript = tmp.filePath(QStringLiteral("nope.txt"));
    {
        QFile f(notAScript);
        f.open(QIODevice::WriteOnly);
        f.write("x\n");
    }
    const auto rejected = runner.run(notAScript, QJsonObject(), error);
    CHECK(!rejected.ok && !error.isEmpty(), "a non-Python script is refused with a reason");

    // The out-parameter is reset on entry, so a stale message from a previous
    // attempt must not survive into the next one.
    error = QStringLiteral("stale");
    const auto refusedAgain = runner.run(notAScript, QJsonObject(), error);
    CHECK(!refusedAgain.ok && error.contains(QStringLiteral("not a Python script")),
          "the refusal reason replaces any stale text");

    const QString interpreter = runner.detectInterpreter();
    // The Windows Store "python3.exe" alias is a re-run stub, not an
    // interpreter; treat it as absent rather than reporting a false failure.
    const bool stubInterpreter =
        interpreter.contains(QStringLiteral("WindowsApps"), Qt::CaseInsensitive);
    if (interpreter.isEmpty() || stubInterpreter)
    {
        std::cout << "[skip] no usable Python interpreter available; transport run not exercised\n";
        std::cout << "python_script_runner_tests: PASS\n";
        return 0;
    }
    std::cout << "[info] interpreter: " << interpreter.toStdString() << "\n";

    const QString script = tmp.filePath(QStringLiteral("echo_boxes.py"));
    QFile file(script);
    CHECK(file.open(QIODevice::WriteOnly | QIODevice::Truncate), "script fixture written");
    file.write(R"(import json, sys
with open(sys.argv[1]) as fh:
    request = json.load(fh)
with open(sys.argv[2], "w") as fh:
    json.dump({"ok": True, "message": "saw " + request["image"],
               "boxes": [[1, 2, 3, 4, "skin"]]}, fh)
)");
    file.close();

    QJsonObject request;
    request.insert(QStringLiteral("image"), tmp.filePath(QStringLiteral("in.png")));
    const auto result = runner.run(script, request, error);
    CHECK(error.isEmpty(), "the script ran without a transport error");
    CHECK(result.ok, "the script response reports success");
    CHECK(result.message.contains(QStringLiteral("saw")), "the response message comes back");
    CHECK(result.boxes.size() == 1 && result.boxes.first().startsWith(QStringLiteral("1,2,3,4,")),
          "the reported boxes come back");

    // A failing script surfaces its stderr as the reason.
    const QString failing = tmp.filePath(QStringLiteral("fail.py"));
    QFile failFile(failing);
    CHECK(failFile.open(QIODevice::WriteOnly | QIODevice::Truncate), "failing fixture written");
    failFile.write("import sys\nsys.stderr.write('boom')\nsys.exit(3)\n");
    failFile.close();
    const auto failed = runner.run(failing, request, error);
    CHECK(!failed.ok && error.contains(QStringLiteral("boom")),
          "a non-zero exit reports the script's stderr");

    if (g_failures != 0)
    {
        std::cout << "python_script_runner_tests: FAIL (" << g_failures << " failures)\n";
        return 1;
    }
    std::cout << "python_script_runner_tests: PASS\n";
    return 0;
}
