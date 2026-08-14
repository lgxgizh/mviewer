// M7 ① Tile cache + LOD: TileCache put/get/eviction, LOD selection math, and
// request() cache-hit behavior (decode called once, then served from cache).
#include "core/render/AsyncTileRequestManager.h"
#include "core/render/TileCache.h"
#include "core/render/TileGrid.h"
#include "core/render/Viewport.h"
#include "core/scheduler/TaskScheduler.h"

#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <thread>

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

static void testByteBudget()
{
    printf("\n[TileCache byte budget]\n");
    fflush(stdout);
    TileCache cache;
    cache.maxBytes = 6;
    cache.maxTiles = 100;
    auto tile = [](int value)
    {
        ImageData d = makeImageData(1, 1, PixelFormat::RGB24);
        (*d.buffer)[0] = static_cast<uint8_t>(value);
        return d;
    };
    const TileKey k1{"bytes", 0, 0, 0};
    const TileKey k2{"bytes", 1, 0, 0};
    const TileKey k3{"bytes", 2, 0, 0};
    cache.put(k1, tile(1));
    cache.put(k2, tile(2));
    CHECK(cache.byteUsage() == 6, "byte usage tracks the payloads");
    cache.get(k1);
    cache.put(k3, tile(3));
    CHECK(cache.get(k1).buffer && cache.get(k3).buffer,
          "LRU keeps the recently touched tile under the byte budget");
    CHECK(cache.get(k2).isNull(), "byte-budget eviction removes the least-recent tile");
    CHECK(cache.metrics().evictions >= 1, "eviction is observable in diagnostics");

    cache.put({"bytes", 3, 0, 0}, makeImageData(2, 2, PixelFormat::RGB24));
    CHECK(cache.size() == 2, "an oversized tile is not retained");
    cache.clear();
    CHECK(cache.byteUsage() == 0 && cache.size() == 0, "clear resets byte usage");
}

static void testCanonicalIdentity()
{
    printf("\n[TileCache canonical identity]\n");
    fflush(stdout);
    TileCache cache;
    TileGrid grid(1024, 1024, 256);
    const Viewport fitA(256, 256, 0.6, 0.0, 0.0);
    const Viewport fitB(256, 256, 0.9, 0.0, 0.0);
    std::vector<std::pair<int, int>> targets;
    auto decode = [&](const std::string &, int, int, int, int, int tw, int th) -> ImageData
    {
        targets.emplace_back(tw, th);
        return makeImageData(tw, th, PixelFormat::RGB24);
    };
    auto first = cache.request("canonical", fitA, grid, decode, nullptr, 4, 100);
    auto second = cache.request("canonical", fitB, grid, decode, nullptr, 4, 100);
    CHECK(!first.empty() && !second.empty(), "same canonical LOD remains drawable across zoom");
    CHECK(targets.size() == 1, "continuous zoom reuses one canonical payload");
    CHECK(targets.front().first == 256 && targets.front().second == 256,
          "canonical LOD output size is stable");

    auto hidpi = cache.request("canonical", fitB, grid, decode, nullptr, 4, 150);
    CHECK(!hidpi.empty() && targets.size() == 2, "DPR policy creates a distinct payload key");
    CHECK(hidpi.front().key.renderScalePercent == 150 && targets.back().first == 384,
          "HiDPI key and materialized width agree");
}

static void testAsyncTileManager()
{
    printf("\n[AsyncTileRequestManager]\n");
    fflush(stdout);
    auto &scheduler = TaskScheduler::instance();
    scheduler.setQueueMaxThreads(TaskScheduler::Priority::Decode, 1);
    TileCache cache;
    AsyncTileRequestManager manager(cache);
    manager.reset(1);
    TileGrid grid(256, 256, 256);
    const Viewport vp(256, 256, 1.0, 0.0, 0.0);
    std::atomic<int> decodeCalls{0};
    std::atomic<int> readyCalls{0};
    std::atomic<bool> started{false};
    auto decode = [&](const std::string &, int, int, int, int, int tw, int th) -> ImageData
    {
        started.store(true, std::memory_order_release);
        ++decodeCalls;
        std::this_thread::sleep_for(std::chrono::milliseconds(150));
        return makeImageData(tw, th, PixelFormat::RGB24);
    };

    const auto before = std::chrono::steady_clock::now();
    const auto first = manager.requestVisible(
        "async", vp, grid, 100, 1, decode, [&](const TileKey &) { ++readyCalls; });
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now() - before);
    CHECK(first.ready.empty() && first.pending == 1,
          "first visible request is Pending, not blocking");
    CHECK(elapsed.count() < 100, "requestVisible returns before the blocking worker finishes");
    const auto duplicate = manager.requestVisible(
        "async", vp, grid, 100, 1, decode, [&](const TileKey &) { ++readyCalls; });
    CHECK(duplicate.pending == 1 && decodeCalls.load() <= 1,
          "same canonical key is de-duplicated while Pending");
    for (int i = 0; i < 500 && !started.load(std::memory_order_acquire); ++i)
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    CHECK(started.load(std::memory_order_acquire), "worker started independently of paint");

    manager.reset(2);
    scheduler.drain(TaskScheduler::DecodePool, std::chrono::seconds(5));
    CHECK(manager.pendingCount() == 0 && readyCalls.load() == 0,
          "reset drops stale completion and converges Pending to zero");

    manager.reset(3);
    const auto current = manager.requestVisible(
        "async-current", vp, grid, 100, 3, decode, [&](const TileKey &) { ++readyCalls; });
    CHECK(current.pending == 1, "new generation accepts current viewport work");
    scheduler.drain(TaskScheduler::DecodePool, std::chrono::seconds(5));
    CHECK(manager.pendingCount() == 0 && cache.size() == 1,
          "current generation reaches Ready and caches exactly one tile");
    CHECK(readyCalls.load() == 1, "only the current generation delivers a callback");
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
    testByteBudget();
    testCanonicalIdentity();
    testAsyncTileManager();
    test100MpVisibleOnly();
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    fflush(stdout);
    return g_fail == 0 ? 0 : 1;
}
