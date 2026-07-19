// Self-contained trace sink implementation. See TraceSink.h.
#include "core/trace/TraceSink.h"

#include <windows.h>

#include <chrono>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <vector>

namespace mviewer::trace
{
namespace
{
struct Span
{
    const char *cat;
    const char *name;
    int64_t tsUs;
    int64_t durUs;
    uint64_t tid;
};

std::mutex g_mu;
std::vector<Span> g_spans;
uint64_t g_pid = 0;

uint64_t currentTid()
{
    // Lightweight thread id (pthread id / Windows thread id). Only used as a
    // label in the trace; exact value is irrelevant.
#if defined(_WIN32)
    return static_cast<uint64_t>(GetCurrentThreadId());
#else
    return reinterpret_cast<uint64_t>(pthread_self());
#endif
}

int64_t nowUs()
{
    return std::chrono::duration_cast<std::chrono::microseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
} // namespace

void record(const char *cat, const char *name, int64_t tsUs, int64_t durUs)
{
    std::lock_guard<std::mutex> lk(g_mu);
    if (g_pid == 0)
        g_pid = static_cast<uint64_t>(GetCurrentProcessId());
    g_spans.push_back({cat, name, tsUs, durUs < 0 ? 0 : durUs, currentTid()});
}

void clear()
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_spans.clear();
}

size_t count()
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_spans.size();
}

bool flush(const std::string &path)
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::ofstream f(path, std::ios::binary);
    if (!f)
        return false;

    f << "{\"traceEvents\":[";
    bool first = true;
    for (const auto &s : g_spans)
    {
        if (!first)
            f << ",";
        first = false;
        // Chrome trace format: duration event (ph="X").
        f << "{\"cat\":\"mviewer." << s.cat << "\","
          << "\"name\":\"" << s.name << "\","
          << "\"ph\":\"X\","
          << "\"ts\":" << s.tsUs << ","
          << "\"dur\":" << s.durUs << ","
          << "\"pid\":" << g_pid << ","
          << "\"tid\":" << s.tid << ","
          << "\"tts\":0}";
    }
    f << "],\"displayTimeUnit\":\"ms\"}";
    return static_cast<bool>(f);
}
} // namespace mviewer::trace
