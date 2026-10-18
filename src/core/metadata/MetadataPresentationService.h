#pragma once

#include "core/image/RawMetadata.h"
#include "core/scheduler/TaskScheduler.h"
#include "domain/Image.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>

namespace mviewer::core
{

// Immutable value delivered to metadata presentation consumers. The source
// pixels are never involved: this is a file-level presentation snapshot only.
struct MetadataPresentationSnapshot
{
    mviewer::domain::ImageMetadata metadata;
    RawMetadata raw;
    std::string identity;

    bool valid() const
    {
        return !metadata.filePath.empty();
    }
};

// Small shared metadata repository for UI presentation. Requests from the
// overlay, MetadataPanel and status bar coalesce into one background read for
// a path. Consumer cancellation is independent, so A -> B -> A and widget
// destruction cannot deliver stale data to a newer consumer.
//
// Delivery contract (see core/MainThreadDispatcher.h): snapshots are deferred
// through the process main-thread dispatcher (the UI installs the Qt
// event-loop dispatcher in main.cpp) and re-check their consumer guard when
// they finally run, so an unregistered consumer is never called. Widget-touching
// consumers marshal themselves as well: a process without an installed
// dispatcher gets inline delivery on the worker thread.
//
// Snapshots are kept in an LRU capped at kDefaultCacheEntries. A hit
// (cached() or a matching in-flight read) becomes most-recently used.
class MetadataPresentationService
{
  public:
    using Snapshot = MetadataPresentationSnapshot;
    using Callback = std::function<void(const Snapshot &)>;
    static constexpr size_t kDefaultCacheEntries = 256;

    static MetadataPresentationService &instance();

    // `consumer` identifies one presentation owner. A later request by the
    // same consumer supersedes its previous request but does not cancel other
    // consumers sharing the same in-flight read.
    uint64_t request(const std::string &path, const std::string &consumer, Callback callback);
    void cancel(const std::string &consumer);

    // Deliberately memory-only. It never validates identity or touches the
    // filesystem; callers that need freshness use request(), whose worker path
    // performs identity validation outside the mutex. A hit refreshes LRU order.
    std::optional<Snapshot> cached(const std::string &path) const;

    size_t cacheSize() const;
    void setCacheCapacity(size_t entries);
    size_t cacheCapacity() const;

  private:
    struct Registration
    {
        std::shared_ptr<std::atomic<bool>> active;
        Callback callback;
    };

    struct Flight
    {
        std::string path;
        std::shared_ptr<std::atomic<bool>> active = std::make_shared<std::atomic<bool>>(true);
        std::unordered_map<std::string, Registration> consumers;
        TaskScheduler::TaskHandle handle;
    };

    struct ConsumerState
    {
        std::shared_ptr<Flight> flight;
        std::shared_ptr<std::atomic<bool>> active;
    };

    struct CacheEntry
    {
        Snapshot snapshot;
        std::list<std::string>::iterator orderIt;
    };

    MetadataPresentationService() = default;
    static std::string fileIdentity(const std::string &path);
    void runFlight(const std::shared_ptr<Flight> &flight);
    void storeSnapshotLocked(const std::string &path, const Snapshot &snapshot);
    void touchSnapshotLocked(std::unordered_map<std::string, CacheEntry>::iterator it) const;
    void evictCacheOverflowLocked(size_t limit);

    mutable std::mutex m_mutex;
    uint64_t m_nextRequestId = 0;
    size_t m_cacheCapacity = kDefaultCacheEntries;
    // Front is most recently used. Mutable so a const cache hit can touch it.
    mutable std::list<std::string> m_cacheOrder;
    mutable std::unordered_map<std::string, CacheEntry> m_cache;
    std::unordered_map<std::string, std::shared_ptr<Flight>> m_flights;
    std::unordered_map<std::string, ConsumerState> m_consumers;
};

} // namespace mviewer::core
