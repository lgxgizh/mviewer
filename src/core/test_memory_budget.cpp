// In-memory cache scale: RAM tiers and the 16 MiB floor.
#include "core/memory/MemoryBudget.h"

#include <cstdio>

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
    } while (0)

using mviewer::core::inMemoryCacheScale;
using mviewer::core::scaledCacheBytes;

int main()
{
    printf("\n[memory budget]\n");

    constexpr size_t kMiB = size_t{1024} * 1024;
    constexpr size_t kGiB = kMiB * 1024;

    CHECK(inMemoryCacheScale(0) == 1.0, "unknown RAM keeps scale 1.0");
    CHECK(inMemoryCacheScale(4 * kGiB) == 0.5, "4 GiB scales to 0.5");
    CHECK(inMemoryCacheScale(8 * kGiB) == 0.75, "8 GiB scales to 0.75");
    CHECK(inMemoryCacheScale(16 * kGiB) == 1.0, "16 GiB scales to 1.0");

    CHECK(scaledCacheBytes(512 * kMiB, 0.5) == 256 * kMiB, "512 MiB * 0.5 = 256 MiB");
    CHECK(scaledCacheBytes(512 * kMiB, 0.01) == 16 * kMiB, "scaled result floors at 16 MiB");
    CHECK(scaledCacheBytes(8 * kMiB, 0.5) == 8 * kMiB, "default below 16 MiB is kept");

    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
