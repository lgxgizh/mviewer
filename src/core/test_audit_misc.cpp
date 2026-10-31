// Focused regressions for audit items that had no home in an existing suite:
// writable-directory cache (#51), async sidecar writes (#53), the file logger
// (#56), and the trace ring (#58).
#include "core/Logger.h"
#include "core/MainThreadDispatcher.h"
#include "core/RatingStore.h"
#include "core/SidecarStore.h"
#include "core/filesystem/AtomicFile.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/trace/TraceSink.h"
#include "runtime_storage.h"

#include <QCoreApplication>
#include <QDir>
#include <QFile>
#include <QStandardPaths>
#include <QString>
#include <QTemporaryDir>
#include <QtGlobal>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <fstream>
#include <sstream>
#include <string>
#include <thread>

static int g_failures = 0;

static void check(bool condition, const char *message)
{
    if (condition)
        return;
    std::fprintf(stderr, "FAIL: %s\n", message);
    ++g_failures;
}

static std::string sidecarFileFor(const std::string &imagePath)
{
    std::string modern = imagePath + ".mviewer.json";
    if (std::ifstream(modern, std::ios::binary))
        return modern;
    return imagePath + ".xmp";
}

static std::string readFile(const std::string &path)
{
    std::ifstream in(path, std::ios::binary);
    std::ostringstream buffer;
    buffer << in.rdbuf();
    return buffer.str();
}

static void testWritableDirectoryCache()
{
    const QString first = mviewer::runtime::writableDirectory(QStandardPaths::CacheLocation);
    const QString second = mviewer::runtime::writableDirectory(QStandardPaths::CacheLocation);
    check(!first.isEmpty() && first == second, "writableDirectory returns a stable cached path");
    check(QDir(first).exists(), "cached writable directory exists");

    mviewer::runtime::resetWritableDirectoryCacheForTests();
    const QString third = mviewer::runtime::writableDirectory(QStandardPaths::CacheLocation);
    check(third == first, "reset hook recomputes the same test-mode path");

    std::string left;
    std::string right;
    std::thread a(
        [&left]()
        {
            left = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation)
                       .toStdString();
        });
    std::thread b(
        [&right]()
        {
            right = mviewer::runtime::writableDirectory(QStandardPaths::AppConfigLocation)
                        .toStdString();
        });
    a.join();
    b.join();
    check(!left.empty() && left == right, "concurrent writableDirectory calls agree");
}

static void testSidecarAsync()
{
    QTemporaryDir dir;
    check(dir.isValid(), "sidecar temp dir");
    const QString imagePath = dir.filePath(QStringLiteral("photo.jpg"));
    QFile image(imagePath);
    check(image.open(QIODevice::WriteOnly), "create sidecar image placeholder");
    image.write("x");
    image.close();

    const std::string path = imagePath.toUtf8().toStdString();
    auto &ratings = mviewer::core::RatingStore::instance();
    for (int stars = 1; stars <= 5; ++stars)
    {
        ratings.setRating(path, stars);
        mviewer::core::SidecarStore::instance().writeSidecarAsync(path);
    }
    check(TaskScheduler::instance().drain(TaskScheduler::IOPool, std::chrono::milliseconds(5000)),
          "sidecar burst drained");
    check(readFile(sidecarFileFor(path)).find("\"rating\": 5") != std::string::npos,
          "later sidecar writes win for the same path");

    std::atomic<int> failures{0};
    mviewer::core::setAtomicWriteFaults({true, false, false});
    ratings.setRating(path, 4);
    mviewer::core::SidecarStore::instance().writeSidecarAsync(path, [&failures](const std::string &)
                                                              { failures.fetch_add(1); });
    check(TaskScheduler::instance().drain(TaskScheduler::IOPool, std::chrono::milliseconds(5000)),
          "failed sidecar write drained");
    check(failures.load() == 1, "sidecar write failure is reported");
    check(readFile(sidecarFileFor(path)).find("\"rating\": 5") != std::string::npos,
          "a failed atomic write leaves the previous sidecar");

    mviewer::core::setAtomicWriteFaults({});
    ratings.setRating(path, 3);
    mviewer::core::SidecarStore::instance().writeSidecarAsync(path, [&failures](const std::string &)
                                                              { failures.fetch_add(1); });
    check(TaskScheduler::instance().drain(TaskScheduler::IOPool, std::chrono::milliseconds(5000)),
          "sidecar retry drained");
    check(failures.load() == 1, "a successful sidecar write does not report failure");
    check(readFile(sidecarFileFor(path)).find("\"rating\": 3") != std::string::npos,
          "sidecar retry writes the latest rating");

    ratings.clearRating(path);
    mviewer::core::setAtomicWriteFaults({});
}

static void testSidecarRejectedSubmitRetries()
{
    struct ResumeIo
    {
        ~ResumeIo()
        {
            TaskScheduler::instance().resume(TaskScheduler::IOPool);
        }
    } resumeIo;

    QTemporaryDir dir;
    check(dir.isValid(), "rejected-submit sidecar temp dir");
    const QString imagePath = dir.filePath(QStringLiteral("photo.jpg"));
    QFile image(imagePath);
    check(image.open(QIODevice::WriteOnly), "create rejected-submit image placeholder");
    image.write("x");
    image.close();

    const std::string path = imagePath.toUtf8().toStdString();
    auto &ratings = mviewer::core::RatingStore::instance();
    auto &sched = TaskScheduler::instance();
    sched.pause(TaskScheduler::IOPool);
    ratings.setRating(path, 1);
    mviewer::core::SidecarStore::instance().writeSidecarAsync(path);
    ratings.setRating(path, 5);
    mviewer::core::SidecarStore::instance().writeSidecarAsync(path);

    // Long enough for the off-thread retry to attempt a submit and fail again
    // while the pool is still paused. An inline fallback would have written.
    std::this_thread::sleep_for(std::chrono::milliseconds(200));
    check(readFile(sidecarFileFor(path)).find("\"rating\"") == std::string::npos,
          "rejected sidecar submit does not write on the caller");

    sched.resume(TaskScheduler::IOPool);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    bool wroteLatest = false;
    while (std::chrono::steady_clock::now() < deadline)
    {
        if (readFile(sidecarFileFor(path)).find("\"rating\": 5") != std::string::npos)
        {
            wroteLatest = true;
            break;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
    }
    check(wroteLatest, "sidecar retry runs off the caller and keeps the latest rating");
    check(sched.drain(TaskScheduler::IOPool, std::chrono::milliseconds(5000)),
          "sidecar retry drained");
    ratings.clearRating(path);
}

static int g_restoredWarnings = 0;

static void restoredMessageHandler(QtMsgType, const QMessageLogContext &, const QString &message)
{
    if (message.contains(QStringLiteral("after-shutdown-token")))
        ++g_restoredWarnings;
}

static void testFileLogger()
{
    qInstallMessageHandler(restoredMessageHandler);
    mviewer::core::installFileLogger("AuditLogger");
    qWarning("audit-warning-token");
    const std::string logPath = mviewer::core::currentLogPath();
    check(!logPath.empty(), "file logger opened a log");
    const std::string body = readFile(logPath);
    check(body.find("audit-warning-token") != std::string::npos,
          "warning is flushed to the log before shutdown");

    std::atomic<int> ready{0};
    std::thread left(
        [&ready]()
        {
            ready.fetch_add(1);
            while (ready.load() < 2)
            {
            }
            qWarning("audit-thread-left");
        });
    std::thread right(
        [&ready]()
        {
            ready.fetch_add(1);
            while (ready.load() < 2)
            {
            }
            qWarning("audit-thread-right");
        });
    left.join();
    right.join();
    const std::string threaded = readFile(logPath);
    check(threaded.find("audit-thread-left") != std::string::npos, "left thread log line flushed");
    check(threaded.find("audit-thread-right") != std::string::npos,
          "right thread log line flushed");

    mviewer::core::shutdownFileLogger();
    qWarning("after-shutdown-token");
    check(g_restoredWarnings == 1, "shutdown restores the previous message handler");
    qInstallMessageHandler(nullptr);
}

static void testTraceRing()
{
    check(mviewer::trace::capacity() == 100000, "trace ring defaults to 100000 events");
    mviewer::trace::clear();
    mviewer::trace::setCapacity(3);
    mviewer::trace::record("cat", "evt-oldest", 1, 1);
    mviewer::trace::record("cat", "evt-2", 2, 1);
    mviewer::trace::record("cat", "evt-3", 3, 1);
    mviewer::trace::record("cat", "evt-4", 4, 1);
    mviewer::trace::record("cat", "evt-newest", 5, 1);
    check(mviewer::trace::count() == 3, "trace ring keeps only the newest events");

    QTemporaryDir dir;
    check(dir.isValid(), "trace temp dir");
    const std::string out = dir.filePath(QStringLiteral("trace.json")).toUtf8().toStdString();
    check(mviewer::trace::flush(out), "trace flush writes JSON");
    const std::string json = readFile(out);
    check(json.find("evt-newest") != std::string::npos, "ring keeps the newest event");
    check(json.find("evt-4") != std::string::npos, "ring keeps the middle retained event");
    check(json.find("evt-3") != std::string::npos, "ring keeps the oldest retained event");
    check(json.find("evt-oldest") == std::string::npos, "ring drops the oldest event");
    check(json.find("evt-2") == std::string::npos, "ring drops the second-oldest event");

    mviewer::trace::clear();
    const std::string tricky = std::string("say \"hi\"\\") + '\x01';
    mviewer::trace::record("c\"at", tricky.c_str(), 9, 1);
    mviewer::trace::record(nullptr, nullptr, 10, 1);
    check(mviewer::trace::count() == 2, "null trace names are stored");
    const std::string escapedPath =
        dir.filePath(QStringLiteral("escaped.json")).toUtf8().toStdString();
    check(mviewer::trace::flush(escapedPath), "escaped trace flush writes JSON");
    const std::string escaped = readFile(escapedPath);
    check(escaped.find("\\\"") != std::string::npos, "trace JSON escapes quotes");
    check(escaped.find("\\\\") != std::string::npos, "trace JSON escapes backslashes");
    check(escaped.find("\\u0001") != std::string::npos, "trace JSON escapes control characters");
    check(escaped.find('\x01') == std::string::npos, "trace JSON does not emit raw control bytes");

    mviewer::trace::clear();
    mviewer::trace::setCapacity(0);
    mviewer::trace::record("cat", "dropped", 1, 1);
    check(mviewer::trace::count() == 0, "zero trace capacity stores nothing");
    mviewer::trace::setCapacity(100000);
}

int main(int argc, char **argv)
{
    QStandardPaths::setTestModeEnabled(true);
    QCoreApplication app(argc, argv);
    QCoreApplication::setOrganizationName(QStringLiteral("MViewerAudit"));
    QCoreApplication::setApplicationName(QStringLiteral("MViewerAuditMisc"));

    testWritableDirectoryCache();
    testSidecarAsync();
    testSidecarRejectedSubmitRetries();
    testFileLogger();
    testTraceRing();

    std::printf("audit_misc_tests: %s (%d failures)\n", g_failures == 0 ? "PASS" : "FAIL",
                g_failures);
    return g_failures == 0 ? 0 : 1;
}
