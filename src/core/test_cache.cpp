// M6 unit tests: CacheManager / DiskCache (5-level cache hierarchy).
#include "core/cache/CacheManager.h"
#include "core/image/DiskCache.h"
#include "core/image/ImageBuffer.h"
#include "core/image/QtConvert.h"

#include <QColor>
#include <QCoreApplication>
#include <QImage>
#include <cstdio>
#include <string>

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
    cfg.maxDiskCacheEntries = 100;
    mgr.configure(cfg);
    CHECK(mgr.config().viewerCacheSize == 8192, "CacheConfig applied");

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

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    printf("=== Cache Tests (M6) ===\n");
    fflush(stdout);

    testCacheManager();
    testCacheManagerM5();
    testCacheConfig();
    testCacheLruEviction();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
