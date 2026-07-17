// Thin trace-event shim for performance instrumentation.
//
// Review P1-10 / P2: requests TRACE_EVENT instrumentation at Decode / Thumbnail /
// Cache / Compare / Render so future Perfetto analysis is straightforward.
//
// Design: a zero-overhead no-op by default. When built with MVIEWER_ENABLE_PERFETTO,
// it forwards to Perfetto's TRACE_EVENT (opt-in backend — Perfetto is NOT a hard
// dependency of the green build, so CI/tests stay dependency-free). The trace *points*
// are wired into the hot paths today; enabling the backend is a single compile flag.
#pragma once

#if defined(MVIEWER_ENABLE_PERFETTO)
#include <perfetto/trace_event.h>
#define MV_TRACE_EVENT(name) TRACE_EVENT0("mviewer", name)
#define MV_TRACE_EVENT1(name, k1, v1) TRACE_EVENT1("mviewer", name, k1, v1)
#define MV_TRACE_SCOPED(name) TRACE_EVENT0("mviewer", name)
#else
// No-op: compiles away, zero runtime cost.
#define MV_TRACE_EVENT(name)                                                                       \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#define MV_TRACE_EVENT1(name, k1, v1)                                                              \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#define MV_TRACE_SCOPED(name)                                                                      \
    do                                                                                             \
    {                                                                                              \
    } while (0)
#endif

// Scoped RAII helper: traces a named region for its lifetime.
//   MV_TRACE_SCOPE("ImageRepository::load");
struct MvTraceScope
{
    // No-op by default; the Perfetto backend provides real scoping via the macro.
    explicit MvTraceScope(const char *)
    {
    }
};
#if defined(MVIEWER_ENABLE_PERFETTO)
#undef MV_TRACE_SCOPED
#define MV_TRACE_SCOPED(name) TRACE_EVENT0("mviewer", name)
#endif
