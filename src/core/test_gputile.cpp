// M16 — GpuTileUploader bookkeeping (headless).
// Verifies the GPU memory tier's logic WITHOUT a GL context: resident-set
// tracking, LRU eviction from GPU memory, and re-upload cache hits. The
// real glTexImage2D upload runs only where a GL context exists (real
// hardware, opt-in via MVIEWER_GPU=1); here we inject the upload/free
// callbacks so the tier's bookkeeping is fully exercised and verified.
#include "gpu/GpuTileUploader.h"

#include <cstdio>
#include <vector>

#if defined(_WIN32)
#include <stdlib.h>
#else
extern "C" int setenv(const char *name, const char *value, int overwrite);
extern "C" int unsetenv(const char *name);
#endif

namespace
{

void setGpuEnv(const char *value)
{
#if defined(_WIN32)
    if (value == nullptr)
        _putenv("MVIEWER_GPU=");
    else
        _putenv_s("MVIEWER_GPU", value);
#else
    if (value == nullptr)
        unsetenv("MVIEWER_GPU");
    else
        setenv("MVIEWER_GPU", value, 1);
#endif
}

} // namespace

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

    // Byte budget evicts even when the texture count would allow more.
    GpuTileUploader bytes([](const TileKey &, const uint8_t *, int, int, int) -> uintptr_t
                          { return 1; });
    bytes.maxResident = 8;
    bytes.maxBytes = 256ull * 256ull * 3ull;
    CHECK(bytes.ensure(k0, nullptr, 256, 256, 3), "byte-budget tile uploaded");
    CHECK(bytes.ensure(k1, nullptr, 256, 256, 3), "second tile uploaded under byte budget");
    CHECK(bytes.residentCount() == 1, "byte budget keeps a single tile");
    CHECK(!bytes.isResident(k0) && bytes.isResident(k1), "byte budget evicts the oldest tile");

    // pinVisible protects on-screen tiles; the unpinned one is the victim.
    GpuTileUploader pinned([](const TileKey &, const uint8_t *, int, int, int) -> uintptr_t
                           { return 7; });
    pinned.maxResident = 2;
    pinned.ensure(k0, nullptr, 32, 32, 3);
    pinned.ensure(k1, nullptr, 32, 32, 3);
    const TileKey pinKeys[] = {k1};
    pinned.pinVisible(pinKeys, 1);
    pinned.ensure(k2, nullptr, 32, 32, 3);
    CHECK(pinned.isResident(k1), "pinned visible tile stays resident");
    CHECK(pinned.isResident(k2), "new tile uploaded beside the pin");
    CHECK(!pinned.isResident(k0), "unpinned tile is the eviction victim");
    CHECK(pinned.uploadCount() == 3, "pinned re-touch did not re-upload");

    // clear() releases everything.
    up.clear();
    CHECK(up.residentCount() == 0, "clear() drops all resident textures");
    CHECK(freed.size() >= 2, "clear() freed all handles");

    // P6: capability probe is callable headless and never lies about a context.
    const bool avail = GpuTileUploader::available();
    CHECK(GpuTileUploader::available() == avail, "available() probe is stable");
    CHECK(!GpuTileUploader::enabled() || GpuTileUploader::available(),
          "enabled() implies available() (no GPU without a context)");

    // A 64x1x3 buffer is 192 bytes. The old fingerprint stepped by n/32 (= 6)
    // and ignored byte 1, so two different tiles skipped the replace.
    std::vector<uint8_t> pixels(192, 10);
    std::vector<uint8_t> same(192, 10);
    std::vector<uint8_t> oneByte = pixels;
    oneByte[1] = static_cast<uint8_t>(oneByte[1] + 1);
    const TileKey fpKey{"fp", 0, 0, 0};
    GpuTileUploader fpUp([](const TileKey &, const uint8_t *, int, int, int) -> uintptr_t
                         { return 3; });
    CHECK(fpUp.ensure(fpKey, pixels.data(), 64, 1, 3), "fingerprint tile uploaded");
    CHECK(fpUp.ensure(fpKey, same.data(), 64, 1, 3), "equal bytes in another buffer stay resident");
    CHECK(fpUp.uploadCount() == 1, "identical pixels do not upload again");
    CHECK(fpUp.replaceCount() == 0, "identical pixels do not replace");
    CHECK(fpUp.ensure(fpKey, oneByte.data(), 64, 1, 3), "one-byte difference stays resident");
    CHECK(fpUp.uploadCount() == 1, "content change keeps the texture object");
    CHECK(fpUp.replaceCount() == 1, "a one-byte change is not treated as the same pixels");
    CHECK(fpUp.ensure(fpKey, nullptr, 64, 1, 3), "null pixels remain a cache hit");
    CHECK(fpUp.replaceCount() == 1, "null pixels do not replace");

    setGpuEnv("1");
    GpuTileUploader::resetEnabledCache();
    CHECK(GpuTileUploader::accelerationRequested(), "MVIEWER_GPU=1 requests acceleration");
    CHECK(!GpuTileUploader::enabled(), "request does not enable without a GL context");
    setGpuEnv("0");
    CHECK(GpuTileUploader::accelerationRequested(), "policy stays cached until reset");
    GpuTileUploader::resetEnabledCache();
    CHECK(!GpuTileUploader::accelerationRequested(), "reset re-reads MVIEWER_GPU=0");
    setGpuEnv("0");
    GpuTileUploader::resetEnabledCache();

    printf("\n=== M16 GpuTileUploader: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
