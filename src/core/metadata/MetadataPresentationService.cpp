#include "core/metadata/MetadataPresentationService.h"

#include "core/FileIdentity.h"
#include "core/MainThreadDispatcher.h"
#include "core/image/FrameSequence.h"
#include "core/image/MetadataReader.h"

#include <QFileInfo>

#include <utility>
#include <vector>

namespace mviewer::core
{

MetadataPresentationService &MetadataPresentationService::instance()
{
    // Keep the process-wide repository alive until process exit. Widget
    // consumers cancel explicitly; avoiding static destruction order here
    // prevents a queued worker delivery from touching torn-down globals.
    // Policy: docs/adr/017-process-lifetime-services.md.
    static auto *service = new MetadataPresentationService();
    return *service;
}

std::string MetadataPresentationService::fileIdentity(const std::string &path)
{
    return fileRevisionMs(path);
}

uint64_t MetadataPresentationService::request(const std::string &path, const std::string &consumer,
                                              Callback callback)
{
    if (path.empty() || consumer.empty() || !callback)
        return 0;

    std::shared_ptr<Flight> flight;
    bool start = false;
    const auto active = std::make_shared<std::atomic<bool>>(true);
    uint64_t requestId = 0;
    TaskScheduler::TaskHandle staleHandle;

    {
        std::lock_guard<std::mutex> lk(m_mutex);
        requestId = ++m_nextRequestId;

        // Supersede this consumer only. Other consumers may still be using the
        // same path and must keep the shared flight alive.
        auto old = m_consumers.find(consumer);
        if (old != m_consumers.end())
        {
            old->second.active->store(false, std::memory_order_release);
            if (old->second.flight)
            {
                const auto oldFlight = old->second.flight;
                oldFlight->consumers.erase(consumer);
                if (oldFlight->consumers.empty())
                {
                    oldFlight->active->store(false, std::memory_order_release);
                    staleHandle = oldFlight->handle;
                    auto oldFlightIt = m_flights.find(oldFlight->path);
                    if (oldFlightIt != m_flights.end() && oldFlightIt->second == oldFlight)
                        m_flights.erase(oldFlightIt);
                }
            }
            m_consumers.erase(old);
        }

        auto it = m_flights.find(path);
        if (it == m_flights.end())
        {
            flight = std::make_shared<Flight>();
            flight->path = path;
            m_flights.emplace(path, flight);
            start = true;
        }
        else
        {
            flight = it->second;
        }

        flight->consumers[consumer] = Registration{active, std::move(callback)};
        m_consumers[consumer] = ConsumerState{flight, active};
    }

    if (staleHandle)
        TaskScheduler::cancel(staleHandle);

    if (!start)
        return requestId;

    auto handle = TaskScheduler::instance().submit(
        TaskScheduler::Priority::Background,
        [this, flight](const TaskScheduler::TaskContext &ctx)
        {
            if (!ctx.isCancelled() && flight->active->load(std::memory_order_acquire))
                runFlight(flight);
        });

    if (handle)
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        if (m_flights.find(path) != m_flights.end())
            flight->handle = handle;
        return requestId;
    }

    std::vector<Callback> rejected;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_flights.find(path);
        if (it != m_flights.end() && it->second == flight)
        {
            for (auto &[key, registration] : flight->consumers)
            {
                if (registration.active->exchange(false) == true)
                    rejected.push_back(std::move(registration.callback));
                m_consumers.erase(key);
            }
            m_flights.erase(it);
        }
    }
    Snapshot empty;
    for (auto &cb : rejected)
        cb(empty);
    return 0;
}

void MetadataPresentationService::cancel(const std::string &consumer)
{
    TaskScheduler::TaskHandle handle;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_consumers.find(consumer);
        if (it == m_consumers.end())
            return;
        it->second.active->store(false, std::memory_order_release);
        const auto flight = it->second.flight;
        if (flight)
        {
            flight->consumers.erase(consumer);
            if (flight->consumers.empty())
            {
                flight->active->store(false, std::memory_order_release);
                handle = flight->handle;
                auto flightIt = m_flights.find(flight->path);
                if (flightIt != m_flights.end() && flightIt->second == flight)
                    m_flights.erase(flightIt);
            }
        }
        m_consumers.erase(it);
    }
    if (handle)
        TaskScheduler::cancel(handle);
}

std::optional<MetadataPresentationService::Snapshot>
MetadataPresentationService::cached(const std::string &path) const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    auto it = m_cache.find(path);
    if (it == m_cache.end())
        return std::nullopt;
    touchSnapshotLocked(it);
    return it->second.snapshot;
}

size_t MetadataPresentationService::cacheSize() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cache.size();
}

void MetadataPresentationService::setCacheCapacity(size_t entries)
{
    std::lock_guard<std::mutex> lk(m_mutex);
    m_cacheCapacity = entries;
    evictCacheOverflowLocked(m_cacheCapacity);
}

size_t MetadataPresentationService::cacheCapacity() const
{
    std::lock_guard<std::mutex> lk(m_mutex);
    return m_cacheCapacity;
}

void MetadataPresentationService::touchSnapshotLocked(
    std::unordered_map<std::string, CacheEntry>::iterator it) const
{
    m_cacheOrder.splice(m_cacheOrder.begin(), m_cacheOrder, it->second.orderIt);
}

void MetadataPresentationService::evictCacheOverflowLocked(size_t limit)
{
    while (m_cache.size() > limit && !m_cacheOrder.empty())
    {
        const std::string victim = m_cacheOrder.back();
        m_cache.erase(victim);
        m_cacheOrder.pop_back();
    }
}

void MetadataPresentationService::storeSnapshotLocked(const std::string &path,
                                                      const Snapshot &snapshot)
{
    auto it = m_cache.find(path);
    if (it != m_cache.end())
    {
        it->second.snapshot = snapshot;
        touchSnapshotLocked(it);
        return;
    }
    if (m_cacheCapacity == 0)
        return;
    evictCacheOverflowLocked(m_cacheCapacity - 1);
    m_cacheOrder.push_front(path);
    CacheEntry entry;
    entry.snapshot = snapshot;
    entry.orderIt = m_cacheOrder.begin();
    m_cache.emplace(path, std::move(entry));
}

void MetadataPresentationService::runFlight(const std::shared_ptr<Flight> &flight)
{
    // All filesystem identity checks and all metadata parsing happen before
    // taking m_mutex. The cache mutex protects values only, never disk I/O.
    const std::string identity = fileIdentity(flight->path);
    Snapshot snapshot;
    bool fromCache = false;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_cache.find(flight->path);
        if (it != m_cache.end() && !identity.empty() && it->second.snapshot.identity == identity)
        {
            touchSnapshotLocked(it);
            snapshot = it->second.snapshot;
            fromCache = true;
        }
    }

    if (!fromCache && !identity.empty() && flight->active->load(std::memory_order_acquire))
    {
        snapshot.metadata = MetadataReader::read(flight->path);
        const auto sequence = FrameSequenceReader::probeSequence(flight->path);
        if (sequence.valid)
        {
            snapshot.metadata.frameCount = sequence.frameCount;
            snapshot.metadata.currentFrame = sequence.defaultFrame;
            snapshot.metadata.durationMs = sequence.totalDurationMs;
            snapshot.metadata.loopCount = sequence.loopCount;
            snapshot.metadata.animated = sequence.animated;
            snapshot.metadata.sequenceKind =
                sequence.kind == FrameSequenceKind::Pages
                    ? "pages"
                    : (sequence.kind == FrameSequenceKind::Animation ? "animation" : "static");
        }
        snapshot.raw = parseRawMetadata(flight->path);
        snapshot.identity = identity;
        if (snapshot.valid())
        {
            std::lock_guard<std::mutex> lk(m_mutex);
            storeSnapshotLocked(flight->path, snapshot);
        }
    }

    std::vector<Callback> callbacks;
    std::vector<std::shared_ptr<std::atomic<bool>>> guards;
    {
        std::lock_guard<std::mutex> lk(m_mutex);
        auto it = m_flights.find(flight->path);
        if (it == m_flights.end() || it->second != flight)
            return;
        for (auto &[consumer, registration] : flight->consumers)
        {
            if (registration.active->load(std::memory_order_acquire))
            {
                callbacks.push_back(std::move(registration.callback));
                guards.push_back(registration.active);
            }
            auto owner = m_consumers.find(consumer);
            if (owner != m_consumers.end() && owner->second.flight == flight)
                m_consumers.erase(owner);
        }
        m_flights.erase(it);
    }

    for (size_t i = 0; i < callbacks.size(); ++i)
    {
        if (!guards[i]->load(std::memory_order_acquire))
            continue;
        const auto callback = std::move(callbacks[i]);
        const auto guard = guards[i];
        // Deferred through the process main-thread dispatcher; the guard is
        // re-checked when the closure finally runs, so a consumer that
        // unregistered in the meantime is never called. Widget-touching
        // consumers marshal themselves as well, so the delivery is safe even
        // when no dispatcher is installed (headless/tests).
        mvcore::postToMainThread(
            [callback, guard, snapshot]()
            {
                if (guard->load(std::memory_order_acquire))
                    callback(snapshot);
            });
    }
}

} // namespace mviewer::core
