// Self-contained trace sink implementation. See TraceSink.h.
#include "core/trace/TraceSink.h"
#include "core/filesystem/Utf8Path.h"

#if defined(_WIN32)
#include <windows.h>
#else
#include <pthread.h>
#include <unistd.h>
#endif

#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <mutex>
#include <ostream>
#include <string>
#include <vector>

namespace mviewer::trace
{
namespace
{
constexpr size_t kDefaultCapacity = 100000;

struct Span
{
    std::string cat;
    std::string name;
    int64_t tsUs = 0;
    int64_t durUs = 0;
    uint64_t tid = 0;
};

std::mutex g_mu;
std::vector<Span> g_spans;
size_t g_capacity = kDefaultCapacity;
size_t g_begin = 0;
size_t g_count = 0;
uint64_t g_pid = 0;

std::string ownedText(const char *text)
{
    return text ? std::string(text) : std::string();
}

void writeJsonEscaped(std::ostream &out, const std::string &value)
{
    static constexpr char kHex[] = "0123456789abcdef";
    for (char ch : value)
    {
        const auto c = static_cast<unsigned char>(ch);
        if (c == '"')
            out << "\\\"";
        else if (c == '\\')
            out << "\\\\";
        else if (c < 0x20)
            out << "\\u00" << kHex[c >> 4] << kHex[c & 0x0F];
        else
            out << static_cast<char>(c);
    }
}

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

void ensurePid()
{
    if (g_pid != 0)
        return;
#if defined(_WIN32)
    g_pid = static_cast<uint64_t>(GetCurrentProcessId());
#else
    g_pid = static_cast<uint64_t>(getpid());
#endif
}

void record(const char *cat, const char *name, int64_t tsUs, int64_t durUs)
{
    std::lock_guard<std::mutex> lk(g_mu);
    ensurePid();
    if (g_capacity == 0)
        return;
    Span span{ownedText(cat), ownedText(name), tsUs, durUs < 0 ? 0 : durUs, currentTid()};
    if (g_spans.size() < g_capacity)
    {
        g_spans.push_back(std::move(span));
        g_count = g_spans.size();
        g_begin = 0;
        return;
    }
    g_spans[g_begin] = std::move(span);
    g_begin = (g_begin + 1) % g_spans.size();
    g_count = g_spans.size();
}

void setCapacity(size_t capacity)
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::vector<Span> ordered;
    if (g_count > 0 && capacity > 0 && !g_spans.empty())
    {
        size_t keep = g_count;
        if (keep > capacity)
            keep = capacity;
        const size_t skip = g_count - keep;
        ordered.reserve(keep);
        for (size_t n = skip; n < g_count; ++n)
            ordered.push_back(std::move(g_spans[(g_begin + n) % g_spans.size()]));
    }
    g_spans = std::move(ordered);
    g_capacity = capacity;
    g_begin = 0;
    g_count = g_spans.size();
}

size_t capacity()
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_capacity;
}

void clear()
{
    std::lock_guard<std::mutex> lk(g_mu);
    g_spans.clear();
    g_begin = 0;
    g_count = 0;
}

size_t count()
{
    std::lock_guard<std::mutex> lk(g_mu);
    return g_count;
}

bool flush(const std::string &path)
{
    std::lock_guard<std::mutex> lk(g_mu);
    std::ofstream f(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!f)
        return false;

    f << "{\"traceEvents\":[";
    bool first = true;
    for (size_t n = 0; n < g_count; ++n)
    {
        const Span &s = g_spans[(g_begin + n) % g_spans.size()];
        if (!first)
            f << ",";
        first = false;
        // Chrome trace format: duration event (ph="X"). Names are escaped so
        // a quote, backslash, or control byte cannot break the JSON.
        f << "{\"cat\":\"mviewer.";
        writeJsonEscaped(f, s.cat);
        f << "\",\"name\":\"";
        writeJsonEscaped(f, s.name);
        f << "\","
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
