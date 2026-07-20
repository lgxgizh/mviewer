#pragma once

#include "core/render/TileCache.h" // TileKey

#include <cstddef>
#include <functional>
#include <string>
#include <unordered_map>
#include <vector>

// GpuTileUploader: the GPU memory tier of the Render Pipeline (M16).
//
// It mirrors TileCache's CPU LRU but tracks which tiles have been uploaded to
// OpenGL textures. Decoded tiles are uploaded ONCE; subsequent paints composite
// from resident GPU textures instead of re-rasterizing / re-converting to
// QImage on the CPU. This is what keeps 100MP pan/zoom smooth.
//
// The component is CAPABILITY-GATED:
//   * available() probes a real GL context. On a headless/offscreen build it
//     returns false, so the whole GPU path is a no-op and the verified CPU
//     compositor (TileCache + QPainter) stays the default.
//   * The actual GL upload (glTexImage2D) is invoked only inside upload() when
//     a context exists. Everything else (resident-set tracking, LRU eviction,
//     re-upload cache hit) is pure logic and is unit-tested headlessly via
//     injected upload/free callbacks — no GL context required for those tests.
//
// Default is OFF; opt in with env MVIEWER_GPU=1 (and a real GL context).
class GpuTileUploader
{
  public:
    // Soft budget: number of tile textures kept resident on the GPU.
    size_t maxResident = 512;

    // Injected texture callbacks so the bookkeeping is unit-testable without
    // a GL context. `upload` is given the pixel bytes of the decoded tile and
    // returns a handle (opaque, e.g. a GL texture id) or 0 on failure.
    // `free` releases a previously-uploaded handle.
    using UploadFn = std::function<uintptr_t(const TileKey &, const uint8_t *pixels, int w, int h,
                                             int channels)>;
    using FreeFn = std::function<void(uintptr_t handle)>;

    GpuTileUploader() = default;
    GpuTileUploader(UploadFn up, FreeFn fr) : m_upload(std::move(up)), m_free(std::move(fr))
    {
    }

    // Probe a real OpenGL context. Returns false on headless/offscreen builds
    // (no shareable GL context) so the GPU path never activates there.
    static bool available();

    // True when GPU compositing is both supported AND opted in. The caller
    // (ImageViewer) uses this to choose between the GPU tier and the CPU path.
    // Opt in with env MVIEWER_GPU=1; requires a real GL context.
    static bool enabled()
    {
        return available() && hasEnvOptIn();
    }

    // Ensure `key` is resident. If already uploaded, it is touched (LRU) and
    // no upload occurs. Otherwise `pixels` are uploaded via m_upload and the
    // handle tracked. Returns true if the tile is now resident.
    bool ensure(const TileKey &key, const uint8_t *pixels, int w, int h, int channels);

    // Release the least-recently-used resident tile if over budget. Returns the
    // freed handle (0 if none freed).
    uintptr_t evictOne();

    // Drop all resident textures (e.g. on image switch).
    void clear();

    bool isResident(const TileKey &key) const;
    size_t residentCount() const
    {
        return m_resident.size();
    }

    // Handles for compositing (caller binds the texture). Returns 0 if not
    // resident.
    uintptr_t handle(const TileKey &key);

  private:
    static bool hasEnvOptIn();

    UploadFn m_upload;
    FreeFn m_free;

    struct Resident
    {
        uintptr_t handle = 0;
        size_t bytes = 0;
    };
    std::unordered_map<TileKey, Resident, TileKeyHash> m_resident;

    // LRU ordering of keys (front = most recent).
    std::vector<TileKey> m_lru;
};
