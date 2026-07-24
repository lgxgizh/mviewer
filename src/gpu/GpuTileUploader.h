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

#include <cstdint>
#include <functional>
#include <list>
#include <string>
#include <unordered_map>

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

    // Drop all resident textures (calls free for each).
    void clear();

  private:
    struct Entry
    {
        uintptr_t handle = 0;
        std::list<TileKey>::iterator lruIt;
    };

    void touch(const TileKey &key);
    void evictIfNeeded();
    uintptr_t doUpload(const TileKey &key, const uint8_t *pixels, int w, int h, int channels);
    void doFree(uintptr_t handle);

    UploadFn m_upload;
    FreeFn m_free;
    std::unordered_map<TileKey, Entry, TileKeyHash> m_map;
    std::list<TileKey> m_lru; // front = oldest
};
