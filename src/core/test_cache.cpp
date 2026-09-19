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
#include <QSet>
#include <QSqlDatabase>
#include <atomic>
#include <barrier>
#include <chrono>
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
    // Two-phase rendezvous (a single barrier is not enough):
    //   workDone — every worker has finished put/get (connections exist).
    //   release  — main has finished inspecting the process-wide Qt SQL
    //              registry, so workers may exit and ThreadConnectionGuard may
    //              removeDatabase().
    // With only one barrier, arrive_and_wait() releases main *and* every worker
    // together; under parallel ctest load workers often tear down their
    // connections before main's connectionNames() poll, which flakes as
    // "observed 0/8 worker connection(s); registry holds 1: mviewer_disk_cache"
    // even though the put/get stress below still passes.
    std::barrier workDone(workerCount + 1);
    std::barrier release(workerCount + 1);
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
                    const std::string key =
                        "m41-thread-" + std::to_string(worker) + "-" + std::to_string(round);
                    const QImage image(8 + (worker % 3), 8 + (round % 3), QImage::Format_RGB32);
                    ImageData data = mvcore::fromQImage(image);
                    disk.put(key, data);
                    ImageData out;
                    if (!disk.get(key, out) || out.width != image.width() ||
                        out.height != image.height())
                        failures.fetch_add(1, std::memory_order_relaxed);
                }
                workDone.arrive_and_wait();
                // Stay alive (connection still registered) until main finishes
                // the distinct-connection ownership check below.
                release.arrive_and_wait();
            });
    }

    // Wait until every worker has opened its thread-affine connection and
    // finished its put/get rounds, then inspect the registry *before* releasing
    // them. Workers must not share one QSqlDatabase across threads.
    workDone.arrive_and_wait();
    QSet<QString> workerConnections;
    const auto connectionDeadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
    while (std::chrono::steady_clock::now() < connectionDeadline)
    {
        workerConnections.clear();
        for (const QString &name : QSqlDatabase::connectionNames())
            if (name.startsWith(QStringLiteral("mviewer_disk_cache_worker_")))
                workerConnections.insert(name);
        if (workerConnections.size() >= workerCount)
            break;
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (workerConnections.size() < workerCount)
    {
        // Full diagnostics: the interesting failure is "the workers clearly did
        // database work (the no-data-loss check below passes) yet the registry
        // shows no worker connection", which needs the whole registry, not just
        // the subset this check looks for.
        const QStringList all = QSqlDatabase::connectionNames();
        printf("  observed %d/%d worker connection(s); registry holds %d:",
               static_cast<int>(workerConnections.size()), workerCount,
               static_cast<int>(all.size()));
        for (const QString &name : all)
            printf(" %s", name.toUtf8().constData());
        printf("\n  disk cache enabled=%d\n", DiskCache::instance().isEnabled() ? 1 : 0);
        fflush(stdout);
    }
    CHECK(workerConnections.size() >= workerCount,
          "each worker owns a distinct process-wide Qt SQL connection");

    // Allow workers to exit so ThreadConnectionGuard can release each connection.
    release.arrive_and_wait();
    for (auto &worker : workers)
        worker.join();

    CHECK(failures.load(std::memory_order_relaxed) == 0,
          "8 workers complete 1280 put/get operations without data loss");

    // ... and releases it when the thread exits (QThreadPool mints fresh threads
    // as idle ones expire, so an unreleased connection per thread leaks a file
    // handle and an SQLite page cache for the whole process lifetime). Only the
    // connections seen above are checked: connections belonging to pool threads
    // another test left running are not this case's business.
    int stillRegistered = 0;
    for (const QString &name : workerConnections)
        if (QSqlDatabase::contains(name))
            ++stillRegistered;
    CHECK(stillRegistered == 0, "worker connections are released when their thread exits");

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
    CHECK(!mgr.getRaw16("raw-erase", out, channels, maxSample), "erase removes the Raw16 key");

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
    CHECK(!mgr.getRaw16("raw-0", out, channels, maxSample), "Raw16 budget evicts the oldest entry");

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
    workers.reserve(8);
    for (int t = 0; t < 8; ++t)
        workers.emplace_back(
            [&, t]
            {
                start.arrive_and_wait();
                for (int i = 0; i < 80; ++i)
                {
                    const std::string key =
                        "raw-thread-" + std::to_string(t) + "-" + std::to_string(i);
                    mgr.putRaw16(key, makeRaw16(32, static_cast<uint16_t>(i)), 1, 65535);
                    if (i % 3 == 0)
                    {
                        std::shared_ptr<std::vector<uint16_t>> localOut;
                        int localChannels = 0;
                        uint16_t localMax = 0;
                        const bool hit = mgr.getRaw16(key, localOut, localChannels, localMax);
                        if (hit && (!localOut || localOut->size() != 32 || localChannels != 1 ||
                                    localMax != 65535))
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

static void testOversizedItemDoesNotFlushCache()
{
    printf("\n[Oversized Entry Protection]\n");
    ImageCache &cache = ImageCache::instance();
    cache.clear();

    const size_t cap = size_t{3} * 16 * 16 * 3; // capacity for 3 items of 768 bytes = 2304 bytes
    cache.setCapacity(ImageCache::Viewer, cap);

    auto mk = [](int i)
    {
        QImage img = makeColorTest(16, 16, QColor(i * 10, i * 5, 255 - i * 10));
        return mvcore::fromQImage(img);
    };

    // Insert 2 small items (total 1536 bytes <= 2304 bytes cap)
    cache.put(ImageCache::Viewer, "small0", mk(0));
    cache.put(ImageCache::Viewer, "small1", mk(1));

    ImageData out;
    CHECK(cache.get(ImageCache::Viewer, "small0", out), "small0 stored");
    CHECK(cache.get(ImageCache::Viewer, "small1", out), "small1 stored");
    CHECK(cache.entryCount(ImageCache::Viewer) == 2, "2 entries stored");

    // Attempt to insert an oversized item (e.g. 100x100 RGB = 30000 bytes > 2304 cap)
    QImage bigImg = makeColorTest(100, 100, QColor(200, 50, 50));
    ImageData bigData = mvcore::fromQImage(bigImg);
    cache.put(ImageCache::Viewer, "oversized", bigData);

    // CRITICAL: The oversized entry must be rejected AND small0 / small1 must NOT be evicted!
    CHECK(!cache.get(ImageCache::Viewer, "oversized", out), "oversized entry rejected");
    CHECK(cache.get(ImageCache::Viewer, "small0", out),
          "small0 survives oversized rejection (no cache flush)");
    CHECK(cache.get(ImageCache::Viewer, "small1", out),
          "small1 survives oversized rejection (no cache flush)");
    CHECK(cache.entryCount(ImageCache::Viewer) == 2,
          "entryCount unchanged after oversized rejection");

    cache.clear();
}

static void testO1LruScalability()
{
    printf("\n[O(1) LRU Scalability]\n");
    ImageCache &cache = ImageCache::instance();
    cache.clear();

    constexpr int kCount = 500;
    // Set capacity to hold exactly 300 entries of 16x16 RGB (768 bytes each)
    constexpr size_t kEntryBytes = size_t{16} * 16 * 3;
    constexpr int kCapEntries = 300;
    cache.setCapacity(ImageCache::Viewer, kCapEntries * kEntryBytes);

    auto mk = [](int i)
    {
        QImage img = makeColorTest(16, 16, QColor((i * 13) % 256, (i * 29) % 256, (i * 47) % 256));
        return mvcore::fromQImage(img);
    };

    // Insert entries 0..499
    for (int i = 0; i < kCount; ++i)
    {
        cache.put(ImageCache::Viewer, "key_" + std::to_string(i), mk(i));
    }

    CHECK(cache.entryCount(ImageCache::Viewer) == kCapEntries,
          "entryCount matches kCapEntries exactly");

    ImageData out;
    // Entries 0..199 must have been evicted in strict FIFO/LRU order
    bool oldEvicted = true;
    for (int i = 0; i < kCount - kCapEntries; ++i)
    {
        if (cache.get(ImageCache::Viewer, "key_" + std::to_string(i), out))
        {
            oldEvicted = false;
            break;
        }
    }
    CHECK(oldEvicted, "first 200 items strictly evicted by LRU");

    // Entries 200..499 must all still be present
    bool recentPresent = true;
    for (int i = kCount - kCapEntries; i < kCount; ++i)
    {
        if (!cache.get(ImageCache::Viewer, "key_" + std::to_string(i), out))
        {
            recentPresent = false;
            break;
        }
    }
    CHECK(recentPresent, "last 300 items strictly retained in cache");

    // Touch key_200 (make it most recent)
    CHECK(cache.get(ImageCache::Viewer, "key_200", out), "touch key_200");

    // Insert one more entry: key_500
    cache.put(ImageCache::Viewer, "key_500", mk(500));

    // Now key_201 should be evicted (as it was the least recently used after key_200 was touched),
    // while key_200 must survive!
    CHECK(!cache.get(ImageCache::Viewer, "key_201", out), "key_201 evicted as LRU");
    CHECK(cache.get(ImageCache::Viewer, "key_200", out), "key_200 retained after O(1) LRU touch");
    CHECK(cache.get(ImageCache::Viewer, "key_500", out), "key_500 retained as most recent");

    cache.clear();
}

static void testMetadataLruAndHardening()
{
    printf("\n[Metadata O(1) LRU & Hardening]\n");
    CacheManager &mgr = CacheManager::instance();
    mgr.clear();

    mviewer::domain::ImageMetadata meta;
    meta.filePath = "/test/meta.png";
    meta.fileSize = 1024;

    // Test empty key handling
    mgr.putMetadata("", meta);
    mviewer::domain::ImageMetadata dummy;
    CHECK(!mgr.getMetadata("", dummy), "getMetadata with empty key returns false");
    CHECK(!mgr.hasMetadata(""), "hasMetadata with empty key returns false");

    // Insert 10 metadata entries
    for (int i = 0; i < 10; ++i)
    {
        meta.fileSize = 1000 + i;
        mgr.putMetadata("meta_" + std::to_string(i), meta);
    }

    // Touch meta_0 (re-promoting it in LRU)
    mviewer::domain::ImageMetadata outMeta;
    CHECK(mgr.getMetadata("meta_0", outMeta) && outMeta.fileSize == 1000,
          "getMetadata touched meta_0");

    // Erase meta_5 with O(1) list splice
    mgr.erase("meta_5");
    CHECK(!mgr.hasMetadata("meta_5"), "meta_5 erased");

    // Invalidate meta_3
    mgr.invalidate("meta_3");
    CHECK(!mgr.hasMetadata("meta_3"), "meta_3 invalidated");

    // Remaining items should all be intact
    CHECK(mgr.hasMetadata("meta_0"), "meta_0 intact");
    CHECK(mgr.hasMetadata("meta_1"), "meta_1 intact");
    CHECK(mgr.hasMetadata("meta_9"), "meta_9 intact");

    mgr.clear();
}

static void testDiskCacheBoundsHardening()
{
    printf("\n[DiskCache Bounds Hardening]\n");
    DiskCache &disk = DiskCache::instance();
    disk.clear();

    ImageData out;
    // Empty key checks
    CHECK(!disk.get("", out), "disk get with empty key returns false");
    disk.remove("");

    // Put image with valid dimensions
    QImage validImg = makeColorTest(16, 16, QColor(80, 80, 80));
    ImageData validData = mvcore::fromQImage(validImg);
    disk.put("valid_key", validData);
    CHECK(disk.get("valid_key", out), "valid disk get succeeds");
    CHECK(out.width == 16 && out.height == 16, "valid disk get dimensions match");

    // Attempt to put an image with pathological dimensions (>256M pixels)
    ImageData huge;
    huge.width = 30000;
    huge.height = 30000; // 900M pixels > 256M cap
    huge.format = PixelFormat::RGB24;
    huge.buffer = std::make_shared<std::vector<uint8_t>>(10); // fake buffer
    disk.put("huge_key", huge);
    CHECK(!disk.get("huge_key", out), "huge image (>256M pixels) rejected by DiskCache");

    disk.clear();
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
    testOversizedItemDoesNotFlushCache();
    testO1LruScalability();
    testMetadataLruAndHardening();
    testDiskCacheBoundsHardening();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
