#pragma once

// M16 / Stage A — GPU tile upload tier (UI layer).
//
// Bookkeeping (resident set, LRU eviction) is unit-tested headlessly via
// injected upload/free callbacks. Real GL upload/free run only when a current
// QOpenGLContext exists and MVIEWER_GPU=1 is set.
//
// Architecture: lives under src/gpu/ (UI boundary). Core/domain stay Qt-free;
// this module may use Qt OpenGL APIs in .cpp only.

#include "core/render/TileCache.h"

#include <cstddef>
#include <cstdint>
#include <functional>
#include <list>
#include <unordered_map>
#include <unordered_set>
#include <vector>

// Uploads decoded tiles to GPU textures and tracks residency.
// When no GL context is available (or MVIEWER_GPU is unset), ensure() is a
// no-op and the CPU QPainter path remains the verified default.
class GpuTileUploader
{
  public:
    // Optional injected callbacks for headless tests. When null, real GL
    // upload/free are used (requires a current QOpenGLContext).
    using UploadFn = std::function<uintptr_t(const TileKey &key, const uint8_t *pixels, int w,
                                             int h, int channels)>;
    using FreeFn = std::function<void(uintptr_t handle)>;

    GpuTileUploader() = default;
    explicit GpuTileUploader(UploadFn upload, FreeFn free = {})
        : m_upload(std::move(upload)), m_free(std::move(free))
    {
    }

    // Soft budget: max resident textures before LRU eviction.
    int maxResident = 256;

    // Byte budget for resident textures (default 256 MiB). Eviction prefers
    // tiles that were not pinned by the current frame, so a pan of ready
    // tiles does not drop the ones still on screen and re-upload them.
    size_t maxBytes = 256ull * 1024ull * 1024ull;

    // True when a real GL context is currently available (or a test injects
    // upload callbacks). Safe to call headless — never throws.
    static bool available();

    // True when available() AND env MVIEWER_GPU is set to a truthy value
    // ("1", "true", "yes", "on"). Opt-in only.
    static bool enabled();

    // Ensure the tile is resident on the GPU. Returns true if a handle is
    // available after the call (upload or cache hit). Returns false when the
    // GPU tier is disabled or upload fails — caller must fall back to CPU.
    bool ensure(const TileKey &key, const uint8_t *pixels, int w, int h, int channels);

    // True if the tile currently has a resident GPU handle.
    bool isResident(const TileKey &key) const;

    // GPU texture handle (GLuint cast to uintptr_t), or 0 if not resident.
    uintptr_t handle(const TileKey &key) const;

    // Number of currently resident textures.
    int residentCount() const
    {
        return static_cast<int>(m_map.size());
    }

    size_t residentBytes() const
    {
        return m_bytes;
    }

    // How many times pixels were actually uploaded (cache hits do not count).
    int uploadCount() const
    {
        return m_uploads;
    }

    // Mark these keys as still on screen before uploading newcomers. Eviction
    // drops unpinned textures first, so already-cached visible tiles stay put.
    void pinVisible(const TileKey *keys, size_t count);

    // Drop all resident textures (calls free for each).
    void clear();

  private:
    struct Entry
    {
        uintptr_t handle = 0;
        std::list<TileKey>::iterator lruIt;
        int w = 0;
        int h = 0;
        int channels = 0;
        size_t bytes = 0;
        uint64_t fingerprint = 0;
    };

    void touch(const TileKey &key);
    void evictIfNeeded();
    void ensureBudgetLoaded();
    uintptr_t doUpload(const TileKey &key, const uint8_t *pixels, int w, int h, int channels);
    bool doReplace(uintptr_t handle, const uint8_t *pixels, int w, int h, int channels);
    void doFree(uintptr_t handle);
    void eraseEntry(std::unordered_map<TileKey, Entry, TileKeyHash>::iterator it);

    UploadFn m_upload;
    FreeFn m_free;
    std::unordered_map<TileKey, Entry, TileKeyHash> m_map;
    std::list<TileKey> m_lru; // front = oldest
    std::unordered_set<TileKey, TileKeyHash> m_pinned;
    size_t m_bytes = 0;
    int m_uploads = 0;
    bool m_budgetLoaded = false;
};
