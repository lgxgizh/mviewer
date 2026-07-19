// Self-contained trace sink for MViewer performance instrumentation.
//
// Part of M13 Phase 5 (Perfetto profiling). The hot paths already carry
// MV_TRACE_* instrumentation points (src/core/trace/Trace.h). When MViewer is
// built with MVIEWER_ENABLE_PERFETTO, those points are forwarded here and
// recorded into an in-memory span buffer. mv_trace_flush() writes the buffer
// as a Chrome trace-format JSON file, which opens directly in the Perfetto
// web UI (ui.perfetto.dev, via its JSON import) and in chrome://tracing.
//
// Why Chrome JSON instead of a binary .perfetto-trace: a real Perfetto trace
// requires vendoring the perfetto SDK (protobuf + large build). To honor the
// AGENTS.md freeze (no new hard dependency in the green build) the sink is
// fully self-contained — hand-written JSON, zero external libraries. The
// timeline data (per-stage durations) is identical and the Perfetto UI
// ingests Chrome JSON natively.
//
// Thread-safety: record() is mutex-guarded; safe to call from worker threads
// (the decode / thumbnail pipelines run off the UI thread).
#pragma once

#include <cstdint>
#include <string>

namespace mviewer::trace
{
// Record a completed duration event.
//   cat   — category (e.g. "decode", "thumb", "render")
//   name  — event name (e.g. "Decoder::decodeFull")
//   tsUs  — start timestamp in microseconds (steady_clock)
//   durUs — duration in microseconds (>= 0)
void record(const char *cat, const char *name, int64_t tsUs, int64_t durUs);

// Flush all recorded spans to a Chrome trace JSON file. Returns true on
// success. The file can be loaded into ui.perfetto.dev or chrome://tracing.
bool flush(const std::string &path);

// Drop all recorded spans without writing (used to bound memory in long runs).
void clear();

// Number of spans currently buffered (for tests / diagnostics).
size_t count();
} // namespace mviewer::trace
