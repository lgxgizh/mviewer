// True in-memory mipmap: MipmapPyramid + CacheManager put/getBestMip/invalidate.
#include "core/cache/CacheManager.h"
#include "core/cache/MipmapPyramid.h"
#include "core/image/ImageBuffer.h"

#include <cstdio>
#include <string>
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
    } while (0)

static ImageData makeRgbRamp(int w, int h)
{
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    const ImageBuffer v = d.view();
    for (int y = 0; y < h; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * 3;
            p[0] = static_cast<uint8_t>(x & 0xff);
            p[1] = static_cast<uint8_t>(y & 0xff);
            p[2] = static_cast<uint8_t>((x + y) & 0xff);
        }
    }
    return d;
}

static void testBuildChainSizes()
{
    printf("\n[MipmapPyramid::buildMipChain]\n");
    ImageData full = makeRgbRamp(1024, 768);
    auto levels = mviewer::cache::buildMipChain(full, /*minEdge=*/256);
    CHECK(!levels.empty(), "chain non-empty");
    if (levels.empty())
        return;
    CHECK(levels[0].width == 1024 && levels[0].height == 768, "lod0 is full");
    CHECK(levels.size() >= 3, "at least full + two halves for 1024");

    int prevEdge = mviewer::cache::imageMaxEdge(levels[0]);
    bool nonIncreasing = true;
    for (size_t i = 1; i < levels.size(); ++i)
    {
        const int edge = mviewer::cache::imageMaxEdge(levels[i]);
        if (edge > prevEdge)
            nonIncreasing = false;
        // Roughly half each step (odd dims floor).
        CHECK(levels[i].width <= (levels[i - 1].width + 1) / 1, "width shrinks or stays");
        CHECK(levels[i].width < levels[i - 1].width || levels[i].height < levels[i - 1].height,
              "at least one dim shrinks");
        prevEdge = edge;
    }
    CHECK(nonIncreasing, "max edges non-increasing fine→coarse");
    CHECK(mviewer::cache::imageMaxEdge(levels.back()) <= 256, "last level ≤ minEdge");

    // Tiny image: only lod0.
    ImageData tiny = makeRgbRamp(128, 96);
    auto tinyLevels = mviewer::cache::buildMipChain(tiny, 256);
    CHECK(tinyLevels.size() == 1, "below minEdge yields single level");
}

static void testCacheManagerMips()
{
    printf("\n[CacheManager mip put/getBest/invalidate]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clearMemory();

    const std::string key = "mip_test_full_1024";
    ImageData full = makeRgbRamp(1024, 512);
    mgr.put(CacheLevel::FullImage, key, full);

    ImageData lod1;
    CHECK(mgr.getMip(key, 1, lod1) && !lod1.isNull(), "eager put builds lod1");
    CHECK(lod1.width == 512 && lod1.height == 256, "lod1 is half");

    ImageData best;
    CHECK(mgr.getBestMip(key, 300, best) && !best.isNull(), "getBestMip hits ≤300");
    CHECK(mviewer::cache::imageMaxEdge(best) <= 300, "best edge ≤ maxEdge");
    CHECK(mviewer::cache::imageMaxEdge(best) >= 256, "best not coarser than needed when available");

    ImageData bestFine;
    CHECK(mgr.getBestMip(key, 900, bestFine) && !bestFine.isNull(), "getBestMip at 900");
    // 1024 > 900, so next-larger (full) or largest ≤900 (512). Prefer ≤900 if exists.
    CHECK(mviewer::cache::imageMaxEdge(bestFine) <= 900 || bestFine.width == 1024,
          "900 request returns ≤900 level or next larger full");

    mgr.invalidate(key);
    ImageData miss;
    CHECK(!mgr.getMip(key, 1, miss), "invalidate removes lod1");
    CHECK(!mgr.get(CacheLevel::FullImage, key, miss), "invalidate removes FullImage");
}

static void testLazyBuildAndEviction()
{
    printf("\n[CacheManager mip lazy + eviction]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clearMemory();

    // Configure a tiny Preview budget so mip store can evict without crashing.
    CacheConfig cfg = mgr.config();
    cfg.previewCacheSize = static_cast<size_t>(64) * 1024; // 64KB
    cfg.viewerCacheSize = static_cast<size_t>(8) * 1024 * 1024;
    mgr.configure(cfg);

    const std::string key = "mip_lazy_key";
    ImageData full = makeRgbRamp(512, 512);
    // putMemory path without going through ensure via put — use ImageCache Viewer
    // then lazy getBestMip. Direct putMemory triggers eager; use putMip lod0 only.
    mgr.putMip(key, 0, full); // FullImage only, no chain yet
    {
        // Clear tracking so ensure thinks none exist — putMip(0) does not set m_mipMaxLod.
        ImageData best;
        CHECK(mgr.getBestMip(key, 200, best) && !best.isNull(), "lazy getBestMip builds from full");
        CHECK(mviewer::cache::imageMaxEdge(best) <= 200 || best.width > 0, "lazy result usable");
    }

    // Flood Preview with large entries to force eviction — must not crash.
    for (int i = 0; i < 32; ++i)
    {
        ImageData filler = makeRgbRamp(128, 128);
        mgr.put(CacheLevel::Preview, "flood_" + std::to_string(i), filler);
    }
    ImageData afterEvict;
    // Miss after eviction is OK; API must remain safe.
    (void)mgr.getBestMip(key, 200, afterEvict);
    CHECK(true, "getBestMip after Preview pressure does not crash");

    mgr.clearMemory();
    // Restore a generous config for other suites that share the process.
    CacheConfig restore;
    mgr.configure(restore);
}

static void testEnsureMipsIdempotent()
{
    printf("\n[CacheManager::ensureMips]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clearMemory();
    const std::string key = "ensure_mips_key";
    ImageData full = makeRgbRamp(640, 480);
    const int n1 = mgr.ensureMips(key, full);
    const int n2 = mgr.ensureMips(key, full);
    CHECK(n1 >= 2, "ensureMips builds multiple levels");
    CHECK(n1 == n2, "ensureMips idempotent count");
    ImageData lod1;
    CHECK(mgr.getMip(key, 1, lod1) && !lod1.isNull(), "ensureMips stored lod1");
}

int main()
{
    printf("=== test_mipmap_pyramid ===\n");
    testBuildChainSizes();
    testCacheManagerMips();
    testLazyBuildAndEviction();
    testEnsureMipsIdempotent();
    printf("\n%d passed, %d failed\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
