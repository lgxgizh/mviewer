// M6 unit tests: CacheManager / DiskCache (5-level cache hierarchy).
#include "core/cache/CacheManager.h"
#include "core/image/DiskCache.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"
#include "core/image/QtConvert.h"
#include "core/perf/MemoryTracker.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <QSqlDatabase>
#include <barrier>
#include <atomic>
#include <cstdio>
#include <string>
#include <thread>
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

static QImage makeColorTest(int w, int h, QColor c)
{
    QImage img(w, h, QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
            img.setPixel(x, y, c.rgb());
    return img;
}

static void testCacheManager()
{
    printf("\n[CacheManager]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clear();

    QImage img = makeColorTest(32, 32, QColor(100, 100, 100));
    ImageData data = mvcore::fromQImage(img);
    std::string key = "test_key_123";

    ImageData miss;
    CHECK(!mgr.get(CacheLevel::FullImage, key, miss), "CacheManager miss returns false");

    mgr.put(CacheLevel::FullImage, key, data);
    CHECK(mgr.memoryUsageBytes() > 0, "memoryUsageBytes > 0 after put");

    ImageData hit;
    CHECK(mgr.get(CacheLevel::FullImage, key, hit), "CacheManager hit returns true");
    CHECK(hit.width == 32 && hit.height == 32, "hit data dimensions match");

    mgr.clearMemory();
    CHECK(mgr.memoryUsageBytes() == 0, "memoryUsageBytes == 0 after clearMemory");
}

// M5 acceptance: 5-level cache hierarchy, disk persistence (survives memory clear /
// restart), and hit-ratio reporting.
static void testCacheManagerM5()
{
    printf("\n[CacheManager M5 — disk persistence + hit ratio]\n");
    fflush(stdout);
    CacheManager &mgr = CacheManager::instance();
    DiskCache &disk = DiskCache::instance();
    mgr.clear();
    disk.clear();

    QImage img = makeColorTest(48, 48, QColor(70, 130, 200));
    ImageData data = mvcore::fromQImage(img);
    const std::string key = "m5_disk_key_1";

    mgr.put(CacheLevel::Disk, key, data);
    CHECK(disk.entryCount() >= 1, "disk tier has an entry after put");
    CHECK(disk.totalBytes() > 0, "disk tier reports >0 bytes");

    mgr.clearMemory();
    CHECK(mgr.memoryUsageBytes() == 0, "memory empty after clear (disk is source of truth)");

    ImageData back;
    CHECK(mgr.getDisk(key, back), "disk get after memory clear succeeds");
    CHECK(back.width == 48 && back.height == 48, "disk pixels dimensions preserved");
    bool identical = back.width == data.width && back.height == data.height;
    if (identical)
    {
        const ImageBuffer vb = back.view(), vd = data.view();
        for (int i = 0; identical && i < back.height; ++i)
        {
            const uint8_t *lb = vb.data + static_cast<size_t>(i) * vb.stride();
            const uint8_t *ld = vd.data + static_cast<size_t>(i) * vd.stride();
            for (int j = 0; j < back.width * 3; ++j)
                if (lb[j] != ld[j])
                {
                    identical = false;
                    break;
                }
        }
    }
    CHECK(identical, "disk-stored pixels are byte-identical to source");

    mgr.clear();
    disk.clear();
    const std::string kHit = "m5_hit", kMiss = "m5_miss";
    mgr.put(CacheLevel::FullImage, kHit, data);
    ImageData out;
    CHECK(mgr.get(CacheLevel::FullImage, kHit, out), "warm get -> hit");
    CHECK(!mgr.get(CacheLevel::FullImage, kMiss, out), "cold get -> miss");
    const CacheLevelStats s = mgr.levelStats(CacheLevel::FullImage);
    CHECK(s.hits >= 1, "FullImage level records a hit");
    CHECK(s.misses >= 1, "FullImage level records a miss");
    const double ratio =
        s.hits + s.misses > 0 ? static_cast<double>(s.hits) / (s.hits + s.misses) : 0.0;
    CHECK(ratio > 0.0 && ratio <= 1.0, "hit ratio in (0,1]");
    printf("  hit ratio = %.3f (hits=%llu misses=%llu)\n", ratio,
           static_cast<unsigned long long>(s.hits), static_cast<unsigned long long>(s.misses));

    mgr.clear();
    disk.clear();
}

static void testDiskCacheThreadAffinityAndStress()
{
    printf("\n[DiskCache M41 — thread affinity and stress]\n");
    DiskCache &disk = DiskCache::instance();
    disk.clear();

    constexpr int workerCount = 8;
    constexpr int rounds = 160;
    std::barrier start(workerCount);
    std::atomic<int> failures{0};
    std::vector<std::thread> workers;
    workers.reserve(workerCount);

    for (int worker = 0; worker < workerCount; ++worker)
    {
        workers.emplace_back(
            [&, worker]
            {
                start.arrive_and_wait();
                for (int round = 0; round < rounds; ++round)
                {
                    const std::string key = "m41-thread-" + std::to_string(worker) + "-" +
                                             std::to_string(round);
                    const QImage image(8 + (worker % 3), 8 + (round % 3), QImage::Format_RGB32);
                    ImageData data = mvcore::fromQImage(image);
                    disk.put(key, data);
                    ImageData out;
                    if (!disk.get(key, out) || out.width != image.width() ||
                        out.height != image.height())
                        failures.fetch_add(1, std::memory_order_relaxed);
                }
            });
    }
    for (auto &worker : workers)
        worker.join();

    CHECK(failures.load(std::memory_order_relaxed) == 0,
          "8 workers complete 1280 put/get operations without data loss");

    int workerConnections = 0;
    for (const QString &name : QSqlDatabase::connectionNames())
        if (name.startsWith(QStringLiteral("mviewer_disk_cache_worker_")))
            ++workerConnections;
    CHECK(workerConnections >= workerCount,
          "each worker owns a distinct process-wide Qt SQL connection");

    disk.clear();
    CHECK(disk.entryCount() == 0, "stress cleanup leaves no disk-cache entries");
}

static void testCacheConfig()
{
    printf("\n[CacheConfig]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clear();

    CacheConfig cfg;
    cfg.metadataCacheSize = 1024;
    cfg.thumbnailCacheSize = 2048;
    cfg.previewCacheSize = 4096;
    cfg.viewerCacheSize = 8192;
    cfg.diskCacheSize = 12345;
    cfg.maxDiskCacheEntries = 100;
    mgr.configure(cfg);
    CHECK(mgr.config().viewerCacheSize == 8192, "CacheConfig applied");
    CHECK(DiskCache::instance().maxBytes() == cfg.diskCacheSize,
          "CacheConfig diskCacheSize is wired to DiskCache maxBytes");

    CacheLevelStats s = mgr.levelStats(CacheLevel::FullImage);
    CHECK(s.bytes == 0, "Empty stats show 0 bytes");

    mviewer::domain::ImageMetadata meta;
    meta.filePath = "/test.png";
    meta.fileSize = 100;
    mgr.putMetadata("key1", meta);
    CHECK(mgr.hasMetadata("key1"), "Metadata stored");
    mviewer::domain::ImageMetadata meta2;
    CHECK(mgr.getMetadata("key1", meta2), "Metadata retrieved");
    CHECK(meta2.fileSize == 100, "Metadata preserved");

    QImage img = makeColorTest(16, 16, QColor(100, 100, 100));
    ImageData data = mvcore::fromQImage(img);
    mgr.put(CacheLevel::FullImage, "eraseKey", data);
    mgr.putMetadata("eraseKey", meta);
    mgr.erase("eraseKey");
    CHECK(!mgr.hasMetadata("eraseKey"), "erase clears metadata");

    mgr.put(CacheLevel::Thumbnail, "invKey", data);
    mgr.putMetadata("invKey", meta);
    mgr.invalidate("invKey");
    CHECK(!mgr.hasMetadata("invKey"), "invalidate clears metadata");
}

static void testCacheLruEviction()
{
    printf("\n[CacheLruEviction]\n");
    ImageCache &cache = ImageCache::instance();
    cache.clear();

    // Review ②: Cache eviction / LRU behavior must be a covered regression area.
    // Force a tiny Viewer capacity so eviction is deterministic.
    const size_t cap = 3 * 16 * 16 * 3; // ~3 small RGB images
    cache.setCapacity(ImageCache::Viewer, cap);

    auto mk = [](int i)
    {
        QImage img = makeColorTest(16, 16, QColor(i * 10, i * 5, 255 - i * 10));
        return mvcore::fromQImage(img);
    };

    // Insert 5 images; capacity allows ~3, so the 2 least-recently-used must go.
    for (int i = 0; i < 5; ++i)
        cache.put(ImageCache::Viewer, "key" + std::to_string(i), mk(i));

    ImageData out;
    CHECK(!cache.get(ImageCache::Viewer, "key0", out), "oldest (key0) evicted");
    CHECK(!cache.get(ImageCache::Viewer, "key1", out), "second-oldest (key1) evicted");
    CHECK(cache.get(ImageCache::Viewer, "key2", out), "key2 retained");
    CHECK(cache.get(ImageCache::Viewer, "key3", out), "key3 retained");
    CHECK(cache.get(ImageCache::Viewer, "key4", out), "key4 (most recent) retained");

    // Touch key2 (make it most-recently-used); insert 2 more to overflow again.
    cache.get(ImageCache::Viewer, "key2", out);
    cache.put(ImageCache::Viewer, "key5", mk(5));
    cache.put(ImageCache::Viewer, "key6", mk(6));

    // Recency order after operations: key6 > key5 > key2(touched) > key4 > key3.
    // Capacity ~3 -> LRU evicts key4 then key3. key2/key5/key6 survive.
    CHECK(cache.get(ImageCache::Viewer, "key2", out), "key2 retained after touch (LRU reorder)");
    CHECK(cache.get(ImageCache::Viewer, "key5", out), "key5 retained (recent)");
    CHECK(cache.get(ImageCache::Viewer, "key6", out), "key6 retained (most recent)");
    CHECK(!cache.get(ImageCache::Viewer, "key4", out), "key4 evicted (LRU after 2nd overflow)");
    CHECK(!cache.get(ImageCache::Viewer, "key3", out), "key3 evicted (was LRU after touch)");

    cache.clear();
}

static std::shared_ptr<std::vector<uint16_t>> makeRaw16(size_t samples, uint16_t value)
{
    return std::make_shared<std::vector<uint16_t>>(samples, value);
}

static void testRaw16CacheM42()
{
    printf("\n[Raw16 cache M42]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clear();
    CacheConfig cfg = mgr.config();
    cfg.raw16CacheSize = 100;
    mgr.configure(cfg);

    auto a = makeRaw16(20, 17); // 40 bytes
    mgr.putRaw16("raw-a", a, 1, 65535);
    CHECK(mgr.raw16UsageBytes() == 40, "Raw16 put reports exact byte accounting");
    CHECK(mgr.memoryUsageBytes() >= 40, "memoryUsageBytes includes Raw16 bytes");
    const auto memorySample = mviewer::perf::MemoryTracker::instance().sample();
    CHECK(memorySample.raw16CacheBytes == 40 && memorySample.cacheTotalBytes >= 40,
          "MemoryTracker observes Raw16 cache bytes");

    mgr.clearMemory();
    CHECK(mgr.raw16UsageBytes() == 0, "clearMemory clears Raw16 cache usage");

    std::weak_ptr<std::vector<uint16_t>> released;
    {
        auto owned = makeRaw16(20, 23);
        released = owned;
        mgr.putRaw16("raw-release", owned, 1, 65535);
        owned.reset();
    }
    mgr.clearMemory();
    CHECK(released.expired(), "clearing Raw16 storage releases its last shared owner");

    mgr.putRaw16("raw-erase", a, 1, 65535);
    mgr.erase("raw-erase");
    std::shared_ptr<std::vector<uint16_t>> out;
    int channels = 0;
    uint16_t maxSample = 0;
    CHECK(!mgr.getRaw16("raw-erase", out, channels, maxSample),
          "erase removes the Raw16 key");

    mgr.putRaw16("raw-invalidate", a, 1, 65535);
    mgr.invalidate("raw-invalidate");
    CHECK(!mgr.getRaw16("raw-invalidate", out, channels, maxSample),
          "invalidate removes the Raw16 key");

    // Three 40-byte entries exceed the 100-byte budget; the least-recently-used
    // entry must be evicted, not retained because an entry-count cap was not hit.
    mgr.putRaw16("raw-0", makeRaw16(20, 0), 1, 65535);
    mgr.putRaw16("raw-1", makeRaw16(20, 1), 1, 65535);
    mgr.putRaw16("raw-2", makeRaw16(20, 2), 1, 65535);
    CHECK(mgr.raw16UsageBytes() <= cfg.raw16CacheSize,
          "Raw16 total usage stays within byte budget");
    CHECK(!mgr.getRaw16("raw-0", out, channels, maxSample),
          "Raw16 budget evicts the oldest entry");

    auto tooLarge = makeRaw16(60, 99); // 120 bytes > 100-byte budget
    mgr.putRaw16("raw-too-large", tooLarge, 1, 65535);
    CHECK(!mgr.getRaw16("raw-too-large", out, channels, maxSample),
          "single Raw16 entry larger than budget is not cached");
    ImageData pixels = makeImageData(2, 2, PixelFormat::RGB24);
    ImageFrame frame({}, pixels);
    frame.setRaw16(tooLarge, 65535, 1);
    uint16_t r = 0, g = 0, b = 0;
    CHECK(frame.raw16At(1, 1, r, g, b) && r == 99 && g == 99 && b == 99,
          "uncached Raw16 remains available through the owning ImageFrame");

    auto reservedTooLarge = std::make_shared<std::vector<uint16_t>>();
    reservedTooLarge->reserve(60); // capacity is 120 bytes although only one sample is live
    reservedTooLarge->push_back(101);
    mgr.putRaw16("raw-reserved-too-large", reservedTooLarge, 1, 65535);
    CHECK(!mgr.getRaw16("raw-reserved-too-large", out, channels, maxSample),
          "Raw16 allocated capacity, not only size, is bounded");

    mgr.clear();
    cfg.raw16CacheSize = 64 * 1024;
    mgr.configure(cfg);
    std::atomic<int> failures{0};
    std::barrier start(8);
    std::vector<std::thread> workers;
    for (int t = 0; t < 8; ++t)
        workers.emplace_back([&, t]
                             {
                                 start.arrive_and_wait();
                                 for (int i = 0; i < 80; ++i)
                                 {
                                     const std::string key = "raw-thread-" + std::to_string(t) +
                                                             "-" + std::to_string(i);
                                     mgr.putRaw16(key, makeRaw16(32, static_cast<uint16_t>(i)), 1,
                                                  65535);
                                     if (i % 3 == 0)
                                     {
                                         std::shared_ptr<std::vector<uint16_t>> localOut;
                                         int localChannels = 0;
                                         uint16_t localMax = 0;
                                         const bool hit = mgr.getRaw16(key, localOut, localChannels,
                                                                        localMax);
                                         if (hit && (!localOut || localOut->size() != 32 ||
                                                     localChannels != 1 || localMax != 65535))
                                             failures.fetch_add(1, std::memory_order_relaxed);
                                     }
                                 }
                             });
    for (auto &worker : workers)
        worker.join();
    CHECK(failures.load() == 0 && mgr.raw16UsageBytes() <= cfg.raw16CacheSize,
          "concurrent Raw16 put/get preserves bounded accounting");

    mgr.clear();
    mgr.configure(CacheConfig{});
}

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Cache Tests (M6) ===\n");
    fflush(stdout);

    testCacheManager();
    testCacheManagerM5();
    testDiskCacheThreadAffinityAndStress();
    testCacheConfig();
    testCacheLruEviction();
    testRaw16CacheM42();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
