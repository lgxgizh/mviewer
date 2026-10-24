#include "benchmark/bench_progress.h"

#include <QByteArray>
#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonValue>
#include <QString>
#include <QtGlobal>

#include <condition_variable>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <iostream>
#include <mutex>
#include <sstream>
#include <thread>

#if defined(_WIN32)
#include <fcntl.h>
#include <io.h>
#else
#include <cerrno>
#include <unistd.h>
#endif

namespace mviewer::bench
{
namespace
{

QtMessageHandler g_prevHandler = nullptr;
std::atomic<int> g_libpngReadErrors{0};

struct StderrTap
{
    int saved = -1;
    int readFd = -1;
    std::thread reader;
};

StderrTap g_stderrTap;

int fdRead(int fd, char *buf, int n)
{
#if defined(_WIN32)
    return _read(fd, buf, n);
#else
    return static_cast<int>(::read(fd, buf, static_cast<size_t>(n)));
#endif
}

int fdWrite(int fd, const char *buf, int n)
{
#if defined(_WIN32)
    return _write(fd, buf, n);
#else
    return static_cast<int>(::write(fd, buf, static_cast<size_t>(n)));
#endif
}

int fdDup(int fd)
{
#if defined(_WIN32)
    return _dup(fd);
#else
    return ::dup(fd);
#endif
}

int fdDup2(int from, int to)
{
#if defined(_WIN32)
    return _dup2(from, to);
#else
    return ::dup2(from, to);
#endif
}

int fdClose(int fd)
{
#if defined(_WIN32)
    return _close(fd);
#else
    return ::close(fd);
#endif
}

bool fdPipe(int fds[2])
{
#if defined(_WIN32)
    return _pipe(fds, 65536, _O_BINARY) == 0;
#else
    return ::pipe(fds) == 0;
#endif
}

bool interruptedRead(int n)
{
#if defined(_WIN32)
    (void)n;
    return false;
#else
    return n < 0 && errno == EINTR;
#endif
}

void writeFdBytes(int fd, const char *data, size_t n)
{
    size_t off = 0;
    while (off < n)
    {
        const size_t left = n - off;
        const int chunk = left > 1 << 20 ? 1 << 20 : static_cast<int>(left);
        const int wrote = fdWrite(fd, data + off, chunk);
        if (wrote <= 0)
            return;
        off += static_cast<size_t>(wrote);
    }
}

void writeFdLine(int fd, const std::string &line)
{
    writeFdBytes(fd, line.data(), line.size());
    writeFdBytes(fd, "\n", 1);
}

bool isLibpngReadErrorLine(const std::string &line)
{
    return line.find("libpng") != std::string::npos && line.find("Read Error") != std::string::npos;
}

// Qt's qpnghandler passes a null libpng error function, so png_default_error
// fprintf's this line. Counting it here is the only path that sees it.
void emitTapLine(const std::string &line, int savedFd)
{
    if (!isLibpngReadErrorLine(line))
    {
        writeFdLine(savedFd, line);
        return;
    }
    const int seen = g_libpngReadErrors.fetch_add(1, std::memory_order_relaxed);
    if (seen < 3)
        writeFdLine(savedFd, line);
    else if (seen == 3)
        writeFdLine(savedFd, "[bench] further libpng Read Error lines suppressed");
}

void appendTapByte(std::string &line, char c, int savedFd)
{
    if (c == '\r')
        return;
    if (c != '\n')
    {
        if (line.size() < 1024)
            line.push_back(c);
        return;
    }
    emitTapLine(line, savedFd);
    line.clear();
}

void consumeTapBytes(std::string &line, const char *data, int n, int savedFd)
{
    for (int i = 0; i < n; ++i)
        appendTapByte(line, data[i], savedFd);
}

void readStderrTap(int readFd, int savedFd)
{
    std::string line;
    char buf[512];
    for (;;)
    {
        const int n = fdRead(readFd, buf, static_cast<int>(sizeof buf));
        if (n > 0)
        {
            consumeTapBytes(line, buf, n, savedFd);
            continue;
        }
        if (n == 0 || !interruptedRead(n))
            break;
    }
    if (!line.empty())
        emitTapLine(line, savedFd);
}

void startStderrTap()
{
    int fds[2] = {-1, -1};
    if (!fdPipe(fds))
        return;
    const int saved = fdDup(2);
    if (saved < 0)
    {
        fdClose(fds[0]);
        fdClose(fds[1]);
        return;
    }
    g_stderrTap.readFd = fds[0];
    g_stderrTap.saved = saved;
    g_stderrTap.reader = std::thread(readStderrTap, fds[0], saved);
    // POSIX dup2 returns the new descriptor; MSVC _dup2 returns 0. Both use -1
    // on failure. A `!= 0` test disables the tap on every non-Windows host.
    if (fdDup2(fds[1], 2) < 0)
    {
        fdClose(fds[1]);
        if (g_stderrTap.reader.joinable())
            g_stderrTap.reader.join();
        fdClose(saved);
        fdClose(fds[0]);
        g_stderrTap.saved = -1;
        g_stderrTap.readFd = -1;
        return;
    }
    fdClose(fds[1]);
}

void forwardQtMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    if (g_prevHandler != nullptr)
    {
        g_prevHandler(type, ctx, msg);
        return;
    }
    const QByteArray bytes = msg.toLocal8Bit();
    if (bytes.isEmpty())
        return;
    std::fwrite(bytes.constData(), 1, static_cast<size_t>(bytes.size()), stderr);
    std::fputc('\n', stderr);
    std::fflush(stderr);
}

void benchQtMessage(QtMsgType type, const QMessageLogContext &ctx, const QString &msg)
{
    forwardQtMessage(type, ctx, msg);
}

void pushRegressionIssue(std::vector<std::string> *issues, const ScenarioResult &result,
                         double delta, double baseline)
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "%s: %+.1f%% (%.3f -> %.3f)", result.name.c_str(), delta,
                  baseline, result.value); // NOLINT(cppcoreguidelines-pro-type-vararg)
    issues->push_back(buf);
}

bool recordBaselineDelta(const ScenarioResult &result,
                         const std::unordered_map<std::string, double> &baseline,
                         const std::set<std::string> &higherIsBetter,
                         std::vector<std::string> *issues)
{
    const std::string key = result.name + "_" + result.metric;
    const auto it = baseline.find(key);
    if (it == baseline.end() || !(it->second > 0.0))
        return true;
    const double delta = ((result.value - it->second) / it->second) * 100.0;
    const bool higher = higherIsBetter.count(result.metric) > 0;
    const bool regressed = higher ? (delta < -10.0) : (delta > 10.0);
    const bool warning = higher ? (delta < -5.0) : (delta > 5.0);
    const char *flag = "";
    if (regressed)
        flag = " *** REGRESSION >10% ***";
    else if (warning)
        flag = " * WARN >5%";
    std::ostringstream line;
    line << "  " << result.name << ": current=" << result.value << " baseline=" << it->second
         << " delta=" << delta << "%" << flag;
    writeLine(line.str());
    if (!regressed)
        return true;
    if (issues != nullptr)
        pushRegressionIssue(issues, result, delta, it->second);
    return false;
}

bool diffBaseline(const std::vector<ScenarioResult> &results,
                  const std::unordered_map<std::string, double> &baseline,
                  const std::set<std::string> &higherIsBetter, std::vector<std::string> *issues)
{
    bool ok = true;
    for (const auto &result : results)
    {
        if (!recordBaselineDelta(result, baseline, higherIsBetter, issues))
            ok = false;
    }
    return ok;
}

void writeHistoryRow(std::ofstream &csv, const std::string &today, const ScenarioResult &result,
                     const std::unordered_map<std::string, double> &baseline)
{
    csv << today << ',' << result.name << ',' << result.metric << ',' << result.value << ','
        << result.timing.p50Ms << ',' << result.timing.p95Ms << ',' << result.timing.p99Ms << ','
        << (result.passed ? "1" : "0") << ',';
    if (!baseline.empty())
    {
        const std::string key = result.name + "_" + result.metric;
        const auto it = baseline.find(key);
        if (it != baseline.end() && it->second > 0.0)
            csv << ((result.value - it->second) / it->second) * 100.0;
    }
    csv << '\n';
}

std::string regressionCell(const ScenarioResult &result,
                           const std::unordered_map<std::string, double> &baseline)
{
    if (baseline.empty())
        return "—";
    const std::string key = result.name + "_" + result.metric;
    const auto it = baseline.find(key);
    if (it == baseline.end() || !(it->second > 0.0))
        return "—";
    const double delta = ((result.value - it->second) / it->second) * 100.0;
    char buf[64];
    std::snprintf(buf, sizeof(buf), "%+.1f%%",
                  delta); // NOLINT(cppcoreguidelines-pro-type-vararg)
    return buf;
}

void writeReportRow(std::ofstream &rpt, const ScenarioResult &result,
                    const std::unordered_map<std::string, double> &baseline)
{
    rpt << "| " << result.name << " | " << result.metric << " | " << result.value << " | "
        << (result.passed ? "PASS" : "FAIL") << " | " << regressionCell(result, baseline) << " |\n";
}

void writeReportIssues(std::ofstream &rpt, const std::vector<std::string> &issues)
{
    if (issues.empty())
    {
        rpt << "\n### No regressions detected\n";
        return;
    }
    rpt << "\n### Regressions (>10%)\n\n";
    for (const auto &issue : issues)
        rpt << "- " << issue << "\n";
}

} // namespace

void installBenchIo()
{
    static std::atomic<bool> installed{false};
    bool expected = false;
    if (!installed.compare_exchange_strong(expected, true))
        return;
    // MSVC treats _IOLBF as fully buffered. _IONBF keeps ctest's pipe from
    // swallowing the banner when the process is killed. libpng fprintf lines
    // are dropped by the stderr tap before they reach that pipe.
    std::setvbuf(stdout, nullptr, _IONBF, 0);
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    std::cout << std::unitbuf;
    std::cerr << std::unitbuf;
    g_prevHandler = qInstallMessageHandler(benchQtMessage);
    startStderrTap();
}

void shutdownBenchIo()
{
    if (g_stderrTap.saved < 0)
        return;
    std::fflush(stderr);
    const int saved = g_stderrTap.saved;
    g_stderrTap.saved = -1;
    // Restoring fd 2 closes the pipe's write end so the reader sees EOF.
    // On failure, close fd 2 first so the reader still observes EOF, then retry.
    if (fdDup2(saved, 2) < 0)
    {
        fdClose(2);
        fdDup2(saved, 2);
    }
    if (g_stderrTap.reader.joinable())
        g_stderrTap.reader.join();
    fdClose(saved);
    if (g_stderrTap.readFd >= 0)
    {
        fdClose(g_stderrTap.readFd);
        g_stderrTap.readFd = -1;
    }
}

int libpngReadErrorCount()
{
    return g_libpngReadErrors.load(std::memory_order_relaxed);
}

void writeLine(const std::string &line)
{
    std::cout << line << '\n';
    std::cout.flush();
    std::fflush(stdout);
}

struct ScenarioSpan::Watchdog
{
    std::atomic<bool> stop{false};
    std::mutex mu;
    std::condition_variable cv;
    std::thread thread;

    Watchdog(const std::string &name, int timeoutMs)
    {
        thread = std::thread(
            [this, name, timeoutMs]
            {
                std::unique_lock<std::mutex> lk(mu);
                const bool stopped = cv.wait_for(lk, std::chrono::milliseconds(timeoutMs), [this]
                                                 { return stop.load(std::memory_order_acquire); });
                if (stopped)
                    return;
                lk.unlock();
                const std::string line =
                    "[bench] WATCHDOG " + name + " exceeded " + std::to_string(timeoutMs) + " ms\n";
                std::fwrite(line.data(), 1, line.size(), stdout);
                std::fwrite(line.data(), 1, line.size(), stderr);
                std::fflush(stdout);
                std::fflush(stderr);
                std::_Exit(2);
            });
    }

    ~Watchdog()
    {
        {
            std::lock_guard<std::mutex> lk(mu);
            stop.store(true, std::memory_order_release);
        }
        cv.notify_all();
        if (thread.joinable())
            thread.join();
    }
};

ScenarioSpan::ScenarioSpan(std::string name, int watchdogTimeoutMs)
    : m_name(std::move(name)), m_startMs(nowMs())
{
    writeLine("[bench] start " + m_name);
    armScenarioDeadline(watchdogTimeoutMs);
    if (watchdogTimeoutMs > 0)
        m_watchdog = std::make_unique<Watchdog>(m_name, watchdogTimeoutMs);
}

void ScenarioSpan::finish()
{
    if (m_finished)
        return;
    m_finished = true;
    disarmScenarioDeadline();
    m_watchdog.reset();
    const long long elapsed = static_cast<long long>(nowMs() - m_startMs);
    writeLine("[bench] done " + m_name + " in " + std::to_string(elapsed) + " ms");
}

ScenarioSpan::~ScenarioSpan()
{
    finish();
}

void writeResultsJson(const std::string &path, const std::vector<ScenarioResult> &results,
                      const BenchResultMeta &meta)
{
    QJsonArray arr;
    for (const auto &result : results)
    {
        QJsonObject o;
        o.insert("name", QString::fromStdString(result.name));
        o.insert("metric", QString::fromStdString(result.metric));
        o.insert("value", result.value);
        o.insert("passed", result.passed);
        arr.append(o);
    }
    QJsonObject root;
    root.insert("results", arr);
    root.insert("hardware_profile", QString::fromStdString(meta.profileName));
    root.insert("logical_cores", static_cast<int>(meta.logicalCores));
    root.insert("timestamp", QDateTime::currentDateTimeUtc().toString(Qt::ISODate));
    QFile f(QString::fromStdString(path));
    if (f.open(QIODevice::WriteOnly | QIODevice::Text))
    {
        f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
        f.close();
    }
}

std::unordered_map<std::string, double> loadBaselineMetrics(const std::string &path)
{
    std::unordered_map<std::string, double> m;
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return m;
    const QByteArray data = f.readAll();
    f.close();

    QJsonParseError err;
    const QJsonDocument doc = QJsonDocument::fromJson(data, &err);
    if (doc.isNull() || !doc.isObject())
        return m;

    const QJsonObject metrics = doc.object().value("metrics").toObject();
    for (auto it = metrics.begin(); it != metrics.end(); ++it)
    {
        if (it.value().isDouble())
            m[it.key().toStdString()] = it.value().toDouble();
        else if (it.value().isString())
            m[it.key().toStdString()] =
                std::strtod(it.value().toString().toStdString().c_str(), nullptr);
    }
    return m;
}

std::string loadBaselineHardwareProfile(const std::string &path)
{
    QFile f(QString::fromStdString(path));
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QJsonDocument doc = QJsonDocument::fromJson(f.readAll());
    f.close();
    if (!doc.isObject())
        return {};
    return doc.object().value("hardware_profile").toString().toStdString();
}

std::string findDefaultBaselineFile()
{
    static const char *const kCandidates[] = {"benchmark/perf_baseline.json",
                                              "../benchmark/perf_baseline.json"};
    for (const char *candidate : kCandidates)
    {
        if (QFile::exists(QString::fromLatin1(candidate)))
            return candidate;
    }
    return {};
}

bool baselineComparisonPassed(const std::string &baselineFile, const std::string &activeProfile,
                              const std::vector<ScenarioResult> &results,
                              const std::set<std::string> &higherIsBetter,
                              std::vector<std::string> *issues)
{
    const std::string baselineProfile = loadBaselineHardwareProfile(baselineFile);
    if (baselineProfile.empty())
    {
        writeLine("Warning: baseline has no hardware_profile; skipping regression comparison.");
        return true;
    }
    if (baselineProfile != activeProfile)
    {
        writeLine("Warning: baseline profile '" + baselineProfile +
                  "' differs from active profile '" + activeProfile +
                  "'; skipping regression comparison.");
        return true;
    }
    const auto baseline = loadBaselineMetrics(baselineFile);
    if (baseline.empty())
    {
        writeLine("Warning: could not load baseline from " + baselineFile);
        return true;
    }
    writeLine("=== REGRESSION CHECK ===");
    return diffBaseline(results, baseline, higherIsBetter, issues);
}

void appendHistory(const std::string &path, const std::vector<ScenarioResult> &results,
                   const std::unordered_map<std::string, double> &baseline)
{
    const bool hasHeader = QFile::exists(QString::fromStdString(path));
    std::ofstream csv(path, std::ios::app);
    if (!csv.is_open())
        return;
    if (!hasHeader)
        csv << "date,scenario,metric,value,p50,p95,p99,passed,regression_pct\n";
    const std::string today = QDateTime::currentDateTime().toString("yyyy-MM-dd").toStdString();
    for (const auto &result : results)
        writeHistoryRow(csv, today, result, baseline);
    csv.close();
    writeLine("history: appended " + std::to_string(results.size()) + " rows to " + path);
}

void writeMarkdownReport(const std::string &path, const std::string &baselineFile,
                         const std::vector<ScenarioResult> &results,
                         const std::unordered_map<std::string, double> &baseline,
                         const std::vector<std::string> &issues)
{
    std::ofstream rpt(path);
    if (!rpt.is_open())
        return;
    rpt << "# MViewer Benchmark Regression Report\n\n";
    rpt << "**Date:** "
        << QDateTime::currentDateTime().toString("yyyy-MM-dd hh:mm:ss").toStdString() << "\n\n";
    rpt << "**Baseline:** " << (baselineFile.empty() ? "(none)" : baselineFile) << "\n\n";
    rpt << "| Scenario | Metric | Value | Passed | Regression |\n";
    rpt << "|----------|--------|-------|--------|------------|\n";
    for (const auto &result : results)
        writeReportRow(rpt, result, baseline);
    writeReportIssues(rpt, issues);
    rpt.close();
    writeLine("report: wrote " + path);
}

} // namespace mviewer::bench
