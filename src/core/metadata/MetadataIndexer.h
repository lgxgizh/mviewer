#pragma once
// M25: generation-scoped asynchronous metadata indexing for the Browse
// workflow.
//
// Before this service, MainWindow's search re-index and ThumbnailPanel's
// camera/lens/ISO index each walked the whole directory and parsed every
// file's metadata synchronously ON THE UI THREAD — duplicated work plus
// multi-second freezes on large folders. MetadataIndexer is the single
// authoritative index: one background pass per directory generation, with
// per-path cache reuse and cancellation.
//
// Qt-free header; the .cpp may use Qt and the TaskScheduler.

#include "core/scheduler/TaskScheduler.h"

#include <cstdint>
#include <functional>
#include <mutex>
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
    using DoneCallback = std::function<void()>;

    static MetadataIndexer &instance();

    // Index `paths` off the UI thread. `onEntry` fires per file, `onDone`
    // once at the end — both marshaled to the main thread. Each call starts
    // a NEW generation and cancels the previous one. Returns the generation
    // token (0 never returned).
    uint64_t index(const std::vector<std::string> &paths, EntryCallback onEntry,
                   DoneCallback onDone);

    // Cancel the current generation; its callbacks are never delivered.
    void cancel();

    // Synchronous no-I/O lookup honoring file identity (mtime/size). Returns
    // nullptr when the file is not indexed or has changed on disk.
    const Entry *cached(const std::string &path) const;

    size_t size() const;

  private:
    MetadataIndexer() = default;
    static std::string fileIdentity(const std::string &path);

    mutable std::mutex m_mtx;
    uint64_t m_gen = 0;
    TaskScheduler::TaskHandle m_handle;
    std::unordered_map<std::string, Entry> m_cache;
    std::unordered_map<std::string, std::string> m_identity; // path -> identity
};

} // namespace mviewer::core
