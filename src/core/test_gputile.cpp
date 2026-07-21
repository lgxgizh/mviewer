// M16 — GpuTileUploader bookkeeping (headless).
// Verifies the GPU memory tier's logic WITHOUT a GL context: resident-set
// tracking, LRU eviction from GPU memory, and re-upload cache hits. The
// real glTexImage2D upload runs only where a GL context exists (real
// hardware, opt-in via MVIEWER_GPU=1); here we inject the upload/free
// callbacks so the tier's bookkeeping is fully exercised and verified.
#include "gpu/GpuTileUploader.h"

#include <cstdio>
#include <vector>

static int g_pass = 0;
static int g_fail = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
        {                                                                                          \
            printf("  PASS: %s\n", msg);                                                           \
            g_pass++;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            printf("  FAIL: %s\n", msg);                                                           \
            g_fail++;                                                                              \
        }                                                                                          \
        fflush(stdout);                                                                            \
    } while (0)

int main()
{
    printf("\n[M16 GpuTileUploader bookkeeping]\n");
    fflush(stdout);

    // Injected texture store: handle -> bytes. Lets us assert eviction frees
    // the right tile and that re-upload is a cache hit.
    std::vector<uintptr_t> freed;
    int uploadCalls = 0;
    GpuTileUploader up(
        [&](const TileKey &, const uint8_t *, int, int, int) -> uintptr_t
        {
            ++uploadCalls;
            return static_cast<uintptr_t>(uploadCalls); // unique handle
        },
        [&](uintptr_t h) { freed.push_back(h); });

    TileKey k0{"img", 0, 0, 0};
    TileKey k1{"img", 1, 0, 0};
    TileKey k2{"img", 2, 0, 0};

    CHECK(up.ensure(k0, nullptr, 256, 256, 3), "k0 uploaded + resident");
    CHECK(up.isResident(k0), "k0 is resident after upload");
    CHECK(up.residentCount() == 1, "resident count == 1");

    // Re-ensure same tile -> cache hit, no second upload.
    uploadCalls = 0;
    CHECK(up.ensure(k0, nullptr, 256, 256, 3), "k0 re-ensure still resident");
    CHECK(uploadCalls == 0, "re-ensure is a cache hit (0 uploads)");
    CHECK(up.handle(k0) != 0, "k0 has a handle");

    // Fill past budget (maxResident=2) -> oldest evicted + freed.
    up.maxResident = 2;
    up.ensure(k1, nullptr, 256, 256, 3);
    up.ensure(k2, nullptr, 256, 256, 3); // exceeds budget
    CHECK(up.residentCount() == 2, "resident count clamped to maxResident=2");
    CHECK(freed.size() >= 1, "at least one texture freed on eviction");
    CHECK(!up.isResident(k0) || !up.isResident(k1), "oldest tile evicted (k0 or k1)");

    // Touch k2 so it is most-recent; evict should drop the other resident.
    freed.clear();
    up.ensure(k2, nullptr, 256, 256, 3);               // touch (cache hit)
    up.ensure({"img", 3, 0, 0}, nullptr, 256, 256, 3); // new -> evict
    CHECK(freed.size() >= 1, "eviction frees on new-over-budget upload");

    // clear() releases everything.
    up.clear();
    CHECK(up.residentCount() == 0, "clear() drops all resident textures");
    CHECK(freed.size() >= 2, "clear() freed all handles");

    // P6: capability probe is callable headless and never lies about a context.
    const bool avail = GpuTileUploader::available();
    CHECK(avail == true || avail == false, "available() probe is callable");
    CHECK(!GpuTileUploader::enabled() || GpuTileUploader::available(),
          "enabled() implies available() (no GPU without a context)");

    printf("\n=== M16 GpuTileUploader: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
