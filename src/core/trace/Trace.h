// Thin trace-event shim for performance instrumentation (M13 Phase 5).
//
// The hot paths already carry MV_TRACE_* instrumentation points. By default
// they compile to no-ops (zero runtime cost). When built with
// MVIEWER_ENABLE_PERFETTO, they forward to the self-contained sink in
// core/trace/TraceSink.{h,cpp}, which records spans and can emit a Chrome
// trace-format JSON file (openable in ui.perfetto.dev / chrome://tracing).
//
// Perfetto (binary .perfetto-trace) would require vendoring the perfetto SDK
// (protobuf + large build). To honor the AGENTS.md freeze (no new hard
// dependency in the green build) the sink is fully self-contained — the
// timeline data is identical and the Perfetto UI ingests Chrome JSON.
//
// Usage:
//   MV_TRACE_EVENT("stage");            // instantaneous marker
//   MV_TRACE_EVENT1("stage","k",v);     // marker with one arg
//   MV_TRACE_SCOPED("Decoder::decode"); // duration of the enclosing scope
//
// At exit (only when tracing is compiled in):
//   mviewer::trace::flush("trace.json");
#pragma once

#include <chrono>
#include <cstdint>

#if defined(MVIEWER_ENABLE_PERFETTO)
#include "core/trace/TraceSink.h"

namespace mviewer::trace
{
// RAII duration scope: records [now, now+dur) under `name` in category `cat`.
struct ScopedTrace
{
    const char *cat_;
    const char *name_;
    int64_t startUs_;
    explicit ScopedTrace(const char *cat, const char *name)
        : cat_(cat), name_(name),
          startUs_(std::chrono::duration_cast<std::chrono::microseconds>(
                       std::chrono::steady_clock::now().time_since_epoch())
                       .count())
    {
    }
    ~ScopedTrace()
    {
        int64_t endUs =
            std::chrono::duration_cast<std::chrono::microseconds>(
                std::chrono::steady_clock::now().time_since_epoch())
                .count();
        record(cat_, name_, startUs_, endUs - startUs_);
    }
};
} // namespace mviewer::trace

#define MV_TRACE_EVENT(name)                                                       \
    do                                                                             \
    {                                                                              \
        mviewer::trace::record("event", name,                                      \
            std::chrono::duration_cast<std::chrono::microseconds>(                 \
                std::chrono::steady_clock::now().time_since_epoch())               \
                .count(),                                                          \
            0);                                                                    \
    } while (0)

#define MV_TRACE_EVENT1(name, k1, v1) MV_TRACE_EVENT(name)

#define MV_TRACE_SCOPED(name)                                                      \
    mviewer::trace::ScopedTrace _mv_scope_##__LINE__("stage", name)

#else
// No-op: compiles away, zero runtime cost.
#define MV_TRACE_EVENT(name)                                                       \
    do                                                                             \
    {                                                                              \
    } while (0)
#define MV_TRACE_EVENT1(name, k1, v1)                                              \
    do                                                                             \
    {                                                                              \
    } while (0)
#define MV_TRACE_SCOPED(name)                                                      \
    do                                                                             \
    {                                                                              \
    } while (0)
#endif
