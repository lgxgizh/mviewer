#pragma once

// Worker-side timing for the current display LOD and neighbor display preloads.
// Not an ImageViewer API. Queue wait is worker entry minus TaskContext::enqueuedAt
// (dependency wait is not included). Decode is this worker's SourceImage::open
// plus raster decode and Sharp fit, stopped before marshal / GUI apply.
// postCancelNs is the part of that decode interval whose phase ended with the
// cancel flag set. A blocking decode does not report the cancel instant, so a
// phase cancelled mid-call is charged in full (upper bound). Scheduling is
// unchanged. This does not measure or fix a multi-second UI stall.

#include "core/scheduler/TaskScheduler.h"

#include <QString>

#include <chrono>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

enum class QueueDecodeRole
{
    CurrentDisplay,
    NeighborPreload,
    Preview
};

struct QueueDecodeSample
{
    std::uint64_t id = 0;
    std::string path;
    QueueDecodeRole role = QueueDecodeRole::CurrentDisplay;
    std::int64_t queueWaitNs = 0;
    std::int64_t decodeNs = 0;
    std::int64_t postCancelNs = 0;
    std::int64_t foregroundYieldNs = 0;
    std::int64_t startedNs = 0; // steady clock epoch, only for relative intervals
    bool started = false;
    bool finished = false;
    bool cancelled = false;
};

struct QueueDecodeTotals
{
    std::uint64_t samples = 0;
    std::int64_t queueWaitNs = 0;
    std::int64_t decodeNs = 0;
    std::int64_t postCancelNs = 0;
};

void resetQueueDecodeSamples();
std::vector<QueueDecodeSample> queueDecodeSamples();
std::optional<QueueDecodeSample> latestQueueDecodeSample(QueueDecodeRole role);
std::optional<QueueDecodeSample> latestQueueDecodeSample(QueueDecodeRole role,
                                                         const std::string &path);
QueueDecodeTotals queueDecodeTotals(QueueDecodeRole role);

// One worker invocation. Construct at entry. beginDecode() just before open.
// finishCurrent() at the marshal boundary. The destructor finishes if needed.
class QueueDecodeSpan
{
  public:
    QueueDecodeSpan(QueueDecodeRole role, const QString &path,
                    const TaskScheduler::TaskContext &ctx);
    ~QueueDecodeSpan();

    QueueDecodeSpan(const QueueDecodeSpan &) = delete;
    QueueDecodeSpan &operator=(const QueueDecodeSpan &) = delete;

    void beginDecode();
    static void closePhase();
    static void finishCurrent();

  private:
    void finish();
    void closePhaseImpl();

    const TaskScheduler::TaskContext *ctx = nullptr;
    QueueDecodeSpan *previous = nullptr;
    std::uint64_t id = 0;
    std::chrono::steady_clock::time_point origin{};
    std::chrono::steady_clock::time_point phaseStart{};
    std::int64_t postCancelNs = 0;
    bool decodeStarted = false;
    bool closed = false;
};
