#pragma once
// M25: generation-scoped asynchronous metadata indexing for the Browse
// workflow. M26: per-request ownership — independent consumers (MainWindow
// search re-index, ThumbnailPanel camera/lens/ISO filters) no longer cancel
// each other; a consumer supersedes ONLY its own stale request.
//
// Before this service, MainWindow's search re-index and ThumbnailPanel's
// camera/lens/ISO index each walked the whole directory and parsed every
// file's metadata synchronously ON THE UI THREAD — duplicated work plus
// multi-second freezes on large folders. MetadataIndexer is the single
// authoritative index: one background pass per request, with per-path cache
// reuse, per-request cancellation and value-semantics cache reads.
//
// Qt-free header; the .cpp may use Qt for file metadata and the TaskScheduler.
//
// Delivery contract (see core/MainThreadDispatcher.h): callbacks are deferred
// through the process main-thread dispatcher — the UI installs the Qt
// event-loop dispatcher in main.cpp — so they run on the GUI thread, in
// submission order, after the worker stopped producing them. A request
// cancelled in that window (worker finished, deliveries still queued) delivers
// nothing. Consumers must NOT rely on the delivery thread for widget safety:
// marshaling is the consumer's job, because a process without an installed
// dispatcher (unit tests, headless tools) gets inline delivery on the worker
// thread.

#include "core/scheduler/TaskScheduler.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <list>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <vector>

namespace mviewer::core
{

// Per-file indexed metadata (one struct per path).
struct MetadataIndexEntry
{
    std::string path;
    std::string searchBlob; // lowercased concatenated searchable text
    std::string camera;     // "make model" (Details column / camera filter)
    std::string lens;       // lens model (Details column / lens filter)
    int iso = 0;            // canonical ISO for the exact-ISO filter
    int64_t width = 0;
    int64_t height = 0;
};

class MetadataIndexer
{
  public:
    using Entry = MetadataIndexEntry;
    using EntryCallback = std::function<void(const Entry &)>;
    using EntryBatchCallback = std::function<void(const std::vector<Entry> &)>;
    using DoneCallback = std::function<void()>;

    static MetadataIndexer &instance();

    // Index `paths` off the UI thread. `onEntry` fires per file, `onDone` once
    // at the end — both deferred through the main-thread dispatcher (see the
    // delivery contract above). Requests are independent: starting one does NOT
    // cancel another consumer's request.
    // Returns the request id (never 0), or 0 when the scheduler rejected the
    // submission (caller must not wait for callbacks in that case).
    uint64_t index(const std::vector<std::string> &paths, const EntryCallback &onEntry,
                   const DoneCallback &onDone);

    // M58: bounded batches reduce UI queue pressure from one queued closure
    // per file to one closure per batch. The batch is a value snapshot and is
    // delivered through the same main-thread dispatcher, in directory order.
    uint64_t indexBatched(const std::vector<std::string> &paths, const EntryBatchCallback &onBatch,
                          const DoneCallback &onDone);

    // Supersede ONE request (its owner's stale work). Its callbacks are never
    // delivered after this point. No-op for unknown/already-finished ids.
    void cancelRequest(uint64_t requestId);

    // Cancel all in-flight requests (shutdown / full reset).
    void cancel();

    // Synchronous no-I/O lookup honoring file identity (mtime/size). Returns a
    // COPY of the entry — safe to keep across later index passes (no dangling
    // pointer into the internal map). nullopt when the file is not indexed or
    // has changed on disk.
    std::optional<Entry> cached(const std::string &path) const;

    size_t size() const;

    // Bounded cache: entries beyond the limit are evicted FIFO (oldest indexed
    // paths first). The default keeps a multi-directory working set without
    // retaining the whole session history.
    size_t cacheLimit() const;
    void setCacheLimit(size_t n);

  private:
    MetadataIndexer() = default;
    static std::string fileIdentity(const std::string &path);
    void eraseRequestLocked(uint64_t requestId);

    mutable std::mutex m_mtx;
    uint64_t m_nextRequestId = 0;
    size_t m_cacheLimit = 100000;
    // Per-request cancellation token; erased when the request finishes.
    std::unordered_map<uint64_t, std::shared_ptr<std::atomic<bool>>> m_requestCancel;
    std::unordered_map<std::string, Entry> m_cache;
    std::unordered_map<std::string, std::string> m_identity; // path -> identity
    std::list<std::string> m_cacheOrder;                     // FIFO eviction order
};

} // namespace mviewer::core
