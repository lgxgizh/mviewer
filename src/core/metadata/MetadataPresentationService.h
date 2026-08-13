#pragma once

#include "core/image/RawMetadata.h"
#include "core/scheduler/TaskScheduler.h"
#include "domain/Image.h"

#include <atomic>
#include <cstdint>
#include <functional>
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
class MetadataPresentationService
{
  public:
    using Snapshot = MetadataPresentationSnapshot;
    using Callback = std::function<void(const Snapshot &)>;

    static MetadataPresentationService &instance();

    // `consumer` identifies one presentation owner. A later request by the
    // same consumer supersedes its previous request but does not cancel other
    // consumers sharing the same in-flight read.
    uint64_t request(const std::string &path, const std::string &consumer, Callback callback);
    void cancel(const std::string &consumer);

    // Deliberately memory-only. It never validates identity or touches the
    // filesystem; callers that need freshness use request(), whose worker path
    // performs identity validation outside the mutex.
    std::optional<Snapshot> cached(const std::string &path) const;

    size_t cacheSize() const;

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

    MetadataPresentationService() = default;
    static std::string fileIdentity(const std::string &path);
    void runFlight(const std::shared_ptr<Flight> &flight);

    mutable std::mutex m_mutex;
    uint64_t m_nextRequestId = 0;
    std::unordered_map<std::string, Snapshot> m_cache;
    std::unordered_map<std::string, std::shared_ptr<Flight>> m_flights;
    std::unordered_map<std::string, ConsumerState> m_consumers;
};

} // namespace mviewer::core
