#include "imageviewer_queuedecode.h"

#include <mutex>
#include <utility>

namespace
{

constexpr std::size_t kQueueDecodeCap = 256;

std::mutex gMu;
std::uint64_t gNextId = 1;
std::vector<QueueDecodeSample> gSamples;
thread_local QueueDecodeSpan *gSpan = nullptr;

std::int64_t elapsedNs(std::chrono::steady_clock::time_point start,
                       std::chrono::steady_clock::time_point end)
{
    if (end <= start)
        return 0;
    const auto ns = std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    return ns > 0 ? static_cast<std::int64_t>(ns) : 0;
}

void pushSample(QueueDecodeSample sample)
{
    if (gSamples.size() == kQueueDecodeCap)
        gSamples.erase(gSamples.begin());
    gSamples.push_back(std::move(sample));
}

std::uint64_t beginSample(QueueDecodeRole role, const std::string &path, std::int64_t queueWaitNs)
{
    std::lock_guard<std::mutex> lock(gMu);
    QueueDecodeSample sample;
    sample.id = gNextId++;
    sample.path = path;
    sample.role = role;
    sample.queueWaitNs = queueWaitNs;
    sample.started = true;
    const std::uint64_t id = sample.id;
    pushSample(std::move(sample));
    return id;
}

void finishSample(std::uint64_t id, std::int64_t decodeNs, std::int64_t postCancelNs,
                  bool cancelled)
{
    if (id == 0)
        return;
    std::lock_guard<std::mutex> lock(gMu);
    for (auto &sample : gSamples)
    {
        if (sample.id != id)
            continue;
        sample.decodeNs = decodeNs;
        sample.postCancelNs = postCancelNs;
        sample.cancelled = cancelled;
        sample.finished = true;
        return;
    }
    QueueDecodeSample sample;
    sample.id = id;
    sample.decodeNs = decodeNs;
    sample.postCancelNs = postCancelNs;
    sample.cancelled = cancelled;
    sample.started = true;
    sample.finished = true;
    pushSample(std::move(sample));
}

} // namespace

void resetQueueDecodeSamples()
{
    std::lock_guard<std::mutex> lock(gMu);
    gSamples.clear();
}

std::vector<QueueDecodeSample> queueDecodeSamples()
{
    std::lock_guard<std::mutex> lock(gMu);
    return gSamples;
}

std::optional<QueueDecodeSample> latestQueueDecodeSample(QueueDecodeRole role)
{
    std::lock_guard<std::mutex> lock(gMu);
    for (auto it = gSamples.rbegin(); it != gSamples.rend(); ++it)
    {
        if (it->role == role)
            return *it;
    }
    return std::nullopt;
}

std::optional<QueueDecodeSample> latestQueueDecodeSample(QueueDecodeRole role,
                                                         const std::string &path)
{
    std::lock_guard<std::mutex> lock(gMu);
    for (auto it = gSamples.rbegin(); it != gSamples.rend(); ++it)
    {
        if (it->role == role && it->path == path)
            return *it;
    }
    return std::nullopt;
}

QueueDecodeTotals queueDecodeTotals(QueueDecodeRole role)
{
    std::lock_guard<std::mutex> lock(gMu);
    QueueDecodeTotals totals;
    for (const auto &sample : gSamples)
    {
        if (sample.role != role || !sample.finished)
            continue;
        ++totals.samples;
        totals.queueWaitNs += sample.queueWaitNs;
        totals.decodeNs += sample.decodeNs;
        totals.postCancelNs += sample.postCancelNs;
    }
    return totals;
}

QueueDecodeSpan::QueueDecodeSpan(QueueDecodeRole role, const QString &path,
                                 const TaskScheduler::TaskContext &context)
    : ctx(&context), previous(gSpan)
{
    gSpan = this;
    const auto entered = std::chrono::steady_clock::now();
    std::int64_t wait = 0;
    if (context.enqueuedAt.time_since_epoch().count() != 0)
        wait = elapsedNs(context.enqueuedAt, entered);
    id = beginSample(role, path.toStdString(), wait);
}

QueueDecodeSpan::~QueueDecodeSpan()
{
    finish();
    if (gSpan == this)
        gSpan = previous;
}

void QueueDecodeSpan::beginDecode()
{
    if (decodeStarted || closed)
        return;
    origin = std::chrono::steady_clock::now();
    phaseStart = origin;
    decodeStarted = true;
}

void QueueDecodeSpan::closePhase()
{
    if (gSpan && !gSpan->closed)
        gSpan->closePhaseImpl();
}

void QueueDecodeSpan::finishCurrent()
{
    if (gSpan)
        gSpan->finish();
}

void QueueDecodeSpan::closePhaseImpl()
{
    if (!decodeStarted || closed)
        return;
    const auto now = std::chrono::steady_clock::now();
    if (ctx->isCancelled())
        postCancelNs += elapsedNs(phaseStart, now);
    phaseStart = now;
}

void QueueDecodeSpan::finish()
{
    if (closed)
        return;
    closed = true;
    std::int64_t decodeNs = 0;
    if (decodeStarted)
    {
        const auto end = std::chrono::steady_clock::now();
        if (ctx->isCancelled())
            postCancelNs += elapsedNs(phaseStart, end);
        phaseStart = end;
        decodeNs = elapsedNs(origin, end);
    }
    const bool cancelled = ctx && ctx->isCancelled();
    finishSample(id, decodeNs, postCancelNs, cancelled);
}
