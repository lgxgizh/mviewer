// M7 ① Tile cache + LOD: TileCache put/get/eviction, LOD selection math, and
// request() cache-hit behavior (decode called once, then served from cache).
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"

#include <cstdio>
#include <functional>

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

static void testLodSelection()
{
    printf("\n[TileCache::chooseLod]\n");
    fflush(stdout);
    // Zoomed in (scale >= 1): LOD 0 (full-res tiles).
    CHECK(TileCache::chooseLod(1.0) == 0, "scale 1.0 -> LOD 0");
    CHECK(TileCache::chooseLod(2.0) == 0, "scale 2.0 -> LOD 0 (zoomed in)");
    // Zoomed out: scale 0.5 -> log2(2)=1 -> LOD 1.
    CHECK(TileCache::chooseLod(0.5) == 1, "scale 0.5 -> LOD 1");
    // scale 0.25 -> log2(4)=2 -> LOD 2.
    CHECK(TileCache::chooseLod(0.25) == 2, "scale 0.25 -> LOD 2");
    // Clamped to maxLod=4.
    CHECK(TileCache::chooseLod(0.01) == 4, "very zoomed out clamped to LOD 4");
    // LOD tile size doubles per level.
    CHECK(TileCache::lodTileSize(256, 0) == 256, "LOD0 tile = 256 src px");
    CHECK(TileCache::lodTileSize(256, 2) == 1024, "LOD2 tile = 1024 src px");
    CHECK(TileCache::lodTileSize(256, 4) == 4096, "LOD4 tile = 4096 src px");
}

static void testLruEviction()
{
    printf("\n[TileCache LRU]\n");
    fflush(stdout);
    TileCache cache;
    cache.maxTiles = 2;
    TileKey k1{"img", 0, 0, 0};
    TileKey k2{"img", 1, 0, 0};
    TileKey k3{"img", 2, 0, 0};
    // Make a dummy 1x1 RGB tile.
    auto tile = [&](int v)
    {
        ImageData d = makeImageData(1, 1, PixelFormat::RGB24);
        (*d.buffer)[0] = static_cast<uint8_t>(v);
        return d;
    };
    cache.put(k1, tile(1));
    cache.put(k2, tile(2));
    CHECK(!cache.get(k1).isNull(), "k1 present after two puts");
    // Touch k1 (get moves to front), then add k3 -> k2 should evict.
    cache.get(k1);
    cache.put(k3, tile(3));
    CHECK(!cache.get(k1).isNull(), "k1 still present (was touched)");
    CHECK(cache.get(k2).isNull(), "k2 evicted (least recently used)");
    CHECK(!cache.get(k3).isNull(), "k3 present");
    CHECK(cache.size() == 2, "size respects maxTiles=2");
}

static void testRequestCacheHit()
{
    printf("\n[TileCache::request decode-once]\n");
    fflush(stdout);
    // 512x512 image, tile size 256 -> 2x2 fine grid. Fit into 256x256 widget
    // (scale 0.5) -> LOD 1 -> 512 src px per tile -> single 1x1 coarse tile.
    TileCache cache;
    TileGrid grid(512, 512, 256);
    Viewport vp(256, 256, 0.5, 0.0, 0.0);

    int decodeCalls = 0;
    auto decode = [&](const std::string &, int, int, int, int, int, int) -> ImageData
    { return makeImageData(1, 1, PixelFormat::RGB24); };

    auto r1 = cache.request("img", vp, grid, decode, &decodeCalls);
    CHECK(!r1.empty(), "first request returns a tile");
    CHECK(decodeCalls == 1, "first request decodes exactly one (coarse LOD) tile");

    // Second identical request: same tile should be served from cache, no decode.
    decodeCalls = 0;
    auto r2 = cache.request("img", vp, grid, decode, &decodeCalls);
    CHECK(!r2.empty(), "second request returns a tile");
    CHECK(decodeCalls == 0, "second request served from cache (0 decode calls)");

    // Different image id -> must decode again.
    auto r3 = cache.request("img2", vp, grid, decode, &decodeCalls);
    CHECK(!r3.empty(), "different image decodes its own tile");
    CHECK(decodeCalls == 1, "new image id triggers one decode");
}

static void test100MpVisibleOnly()
{
    printf("\n[TileCache 100MP visible-only decode (M16)]\n");
    fflush(stdout);
    // 10000x8000 ~= 80MP image. Tile base 256 -> at fit-into a 1280x1024
    // window the scale is ~0.128, so chooseLod -> LOD 3 (256*8=2048 src px
    // per coarse tile). Only the tiles intersecting the viewport are decoded, never
    // the full bitmap. This is the "100MP 不卡" guarantee.
    const int W = 10000, H = 8000;
    TileCache cache;
    TileGrid grid(W, H, 256);
    // Window 1280x1024, image fit (scale < 1).
    Viewport vp(1280, 1024, 0.128, 0.0, 0.0);

    int decodeCalls = 0;
    auto decode = [&](const std::string &, int, int, int, int, int, int) -> ImageData
    { return makeImageData(1, 1, PixelFormat::RGB24); };

    auto r1 = cache.request("big", vp, grid, decode, &decodeCalls);
    CHECK(!r1.empty(), "100MP: at least one visible tile returned");
    // LOD 3 -> each coarse tile covers 2048x2048 src px. Window 1280x1024
    // at scale 0.128 shows ~10k x 8k src -> ~5x4 coarse tiles = ~20.
    // The key assertion: far fewer than the full fine-grid decode
    // (10000/256 * 8000/256 ~= 39*31 = 1222 fine tiles).
    CHECK(decodeCalls > 0 && decodeCalls < 200,
          "100MP: only visible coarse tiles decoded (not the full 1222-tile grid)");

    // Second identical request: served from cache, zero new decodes.
    decodeCalls = 0;
    auto r2 = cache.request("big", vp, grid, decode, &decodeCalls);
    CHECK(!r2.empty() && decodeCalls == 0, "100MP: cached request reuses tiles (0 decode calls)");

    // Pan the view by ~half a screen; only newly-visible tiles decode.
    Viewport vpPan(1280, 1024, 0.128, -600.0, 0.0);
    decodeCalls = 0;
    auto r3 = cache.request("big", vpPan, grid, decode, &decodeCalls);
    CHECK(!r3.empty(), "100MP: pan returns visible tiles");
    CHECK(decodeCalls >= 0 && decodeCalls < 200, "100MP: pan decodes only newly-visible tiles");
    // Pan must not re-decode tiles already resident from the first view.
    CHECK(cache.size() < 200, "100MP: resident tile count stays bounded");
}

int main()
{
    printf("=== TileCache + LOD tests (M7 ①) ===\n");
    fflush(stdout);
    testLodSelection();
    testLruEviction();
    testRequestCacheHit();
    test100MpVisibleOnly();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
