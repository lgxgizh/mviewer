#pragma once

#include "benchmark/scenarios.h"

#include <atomic>
#include <chrono>
#include <memory>
#include <set>
#include <string>
#include <unordered_map>
#include <vector>

namespace mviewer::bench
{

// Lower-is-better stand-in so a timed-out measurement fails a numeric budget.
// Higher-is-better metrics use 0 instead (see timeoutResult).
inline constexpr double kTimeoutSentinel = 1.0e18;
inline constexpr int kDefaultScenarioTimeoutMs = 120000;

// Unbuffered stdout (MSVC treats _IOLBF as fully buffered). stderr is tapped
// so libpng's own fprintf("libpng error: Read Error") — Qt's PNG handler
// installs a null libpng error callback, so the Qt message handler never sees
// it — is rate-limited before it reaches ctest's pipe.
void installBenchIo();
void shutdownBenchIo();
int libpngReadErrorCount();

void writeLine(const std::string &line);

// Armed by ScenarioSpan for the scenario that is running. Disarmed otherwise,
// so unit tests that call scenarios directly never trip the deadline.
inline long long steadyNowMs()
{
    using Clock = std::chrono::steady_clock;
    return std::chrono::duration_cast<std::chrono::milliseconds>(Clock::now().time_since_epoch())
        .count();
}

struct ScenarioDeadlineState
{
    std::atomic<long long> deadlineMs{-1};
    std::atomic<int> budgetMs{0};
};

inline ScenarioDeadlineState &scenarioDeadlineState()
{
    static ScenarioDeadlineState state;
    return state;
}

inline void armScenarioDeadline(int timeoutMs)
{
    auto &state = scenarioDeadlineState();
    if (timeoutMs <= 0)
    {
        state.budgetMs.store(0, std::memory_order_release);
        state.deadlineMs.store(-1, std::memory_order_release);
        return;
    }
    state.budgetMs.store(timeoutMs, std::memory_order_release);
    state.deadlineMs.store(steadyNowMs() + static_cast<long long>(timeoutMs),
                           std::memory_order_release);
}

inline void disarmScenarioDeadline()
{
    armScenarioDeadline(0);
}

inline int scenarioDeadlineBudgetMs()
{
    return scenarioDeadlineState().budgetMs.load(std::memory_order_acquire);
}

inline bool scenarioPastDeadline()
{
    const long long deadline = scenarioDeadlineState().deadlineMs.load(std::memory_order_acquire);
    if (deadline < 0)
        return false;
    return steadyNowMs() >= deadline;
}

inline ScenarioResult timeoutResult(const std::string &name, const std::string &metric,
                                    int timeoutMs, bool higherIsBetter)
{
    ScenarioResult r;
    r.name = name;
    r.metric = metric;
    r.value = higherIsBetter ? 0.0 : kTimeoutSentinel;
    r.passed = false;
    r.detail = "timed out after " + std::to_string(timeoutMs) + " ms";
    return r;
}

// Fills `r` and returns true when the armed per-scenario deadline has passed.
inline bool failIfScenarioTimedOut(ScenarioResult &r, bool higherIsBetter)
{
    if (!scenarioPastDeadline())
        return false;
    const ScenarioResult timed =
        timeoutResult(r.name, r.metric, scenarioDeadlineBudgetMs(), higherIsBetter);
    r.value = timed.value;
    r.passed = false;
    r.detail = timed.detail;
    return true;
}

// Prints "[bench] start <name>" / "[bench] done <name> in N ms". When
// `watchdogTimeoutMs` > 0, a thread calls std::_Exit(2) if finish() is not
// reached in time (a blocked QImageReader cannot be cancelled cooperatively).
class ScenarioSpan
{
  public:
    explicit ScenarioSpan(std::string name, int watchdogTimeoutMs);
    ~ScenarioSpan();
    ScenarioSpan(const ScenarioSpan &) = delete;
    ScenarioSpan &operator=(const ScenarioSpan &) = delete;
    void finish();

  private:
    struct Watchdog;
    std::string m_name;
    double m_startMs = 0;
    bool m_finished = false;
    std::unique_ptr<Watchdog> m_watchdog;
};

struct BenchResultMeta
{
    std::string profileName;
    unsigned logicalCores = 1;
};

void writeResultsJson(const std::string &path, const std::vector<ScenarioResult> &results,
                      const BenchResultMeta &meta);
std::unordered_map<std::string, double> loadBaselineMetrics(const std::string &path);
std::string loadBaselineHardwareProfile(const std::string &path);
std::string findDefaultBaselineFile();

// True when the comparison was skipped or no metric regressed more than 10%.
bool baselineComparisonPassed(const std::string &baselineFile, const std::string &activeProfile,
                              const std::vector<ScenarioResult> &results,
                              const std::set<std::string> &higherIsBetter,
                              std::vector<std::string> *issues);

void appendHistory(const std::string &path, const std::vector<ScenarioResult> &results,
                   const std::unordered_map<std::string, double> &baseline);
void writeMarkdownReport(const std::string &path, const std::string &baselineFile,
                         const std::vector<ScenarioResult> &results,
                         const std::unordered_map<std::string, double> &baseline,
                         const std::vector<std::string> &issues);

} // namespace mviewer::bench
