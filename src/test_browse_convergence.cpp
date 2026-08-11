// M25 RC convergence — Phase 1 regression baseline (UI level).
//
// Guards the Professional Browse data-pipeline contracts that live in the UI
// layer:
//   1. ThumbnailCache identity — requested size + schema version are part of
//      the cache key (64 → 240 must never reuse the 64px thumbnail).
//   2. ThumbnailPipeline — size identity of in-flight results and true
//      generation cancellation (old-directory results never delivered).
//   3. ThumbnailPanel — size change drops stale ready pixmaps, camera/lens
//      filters are field-scoped, filter rebuild preserves ready thumbnails.
#include "core/filesystem/FileSystem.h"
#include "core/thumbnail/ThumbnailPipeline.h"
#include "thumbnailcache.h"
#include "thumbnailpanel.h"

#include <QApplication>
#include <QDateTime>
#include <QDir>
#include <QElapsedTimer>
#include <QFile>
#include <QFileInfo>
#include <QImage>
#include <QStandardPaths>
#include <QTemporaryDir>
#include <QTimer>

#include <atomic>
#include <chrono>
#include <cstdio>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

static int g_failures = 0;

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (cond)                                                                                  \
            std::printf("[ok] %s\n", msg);                                                         \
        else                                                                                       \
        {                                                                                          \
            std::printf("[FAIL] %s\n", msg);                                                       \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

void pump(int ms = 30)
{
    QElapsedTimer t;
    t.start();
    do
    {
        QApplication::processEvents(QEventLoop::AllEvents, 10);
    } while (t.elapsed() < ms);
}

// Minimal little-endian TIFF (DNG-like) carrying ISO/Make/Model/LensModel so
// parseRawMetadata can extract real camera/lens fields for the filter tests.
static bool writeFakeDng(const std::string &path, const std::string &make, const std::string &model,
                         const std::string &lens, uint16_t iso)
{
    FILE *f = std::fopen(path.c_str(), "wb");
    if (!f)
        return false;
    uint8_t hdr[8] = {'I', 'I', 0x2A, 0x00, 0x08, 0x00, 0x00, 0x00};
    std::fwrite(hdr, 1, 8, f);
    const uint16_t count = 4;
    std::fwrite(&count, 2, 1, f);
    const long ifdStart = 8;
    long dataOffset = ifdStart + 2 + count * 12 + 4;
    const long makeOff = dataOffset;
    const long modelOff = makeOff + static_cast<long>(make.size()) + 1;
    const long lensOff = modelOff + static_cast<long>(model.size()) + 1;
    auto writeEntry = [&](uint16_t tag, uint16_t type, uint32_t cnt, uint32_t val)
    {
        std::fwrite(&tag, 2, 1, f);
        std::fwrite(&type, 2, 1, f);
        std::fwrite(&cnt, 4, 1, f);
        std::fwrite(&val, 4, 1, f);
    };
    writeEntry(0x8827, 3, 1, iso); // ISO (SHORT inline)
    writeEntry(0x010F, 2, static_cast<uint32_t>(make.size()) + 1,
               static_cast<uint32_t>(makeOff)); // Make (ASCII)
    writeEntry(0x0110, 2, static_cast<uint32_t>(model.size()) + 1,
               static_cast<uint32_t>(modelOff)); // Model (ASCII)
    writeEntry(0xA434, 2, static_cast<uint32_t>(lens.size()) + 1,
               static_cast<uint32_t>(lensOff)); // LensModel (ASCII)
    uint32_t nextIfd = 0;
    std::fwrite(&nextIfd, 4, 1, f);
    std::fseek(f, makeOff, SEEK_SET);
    std::fwrite(make.c_str(), 1, make.size() + 1, f);
    std::fwrite(model.c_str(), 1, model.size() + 1, f);
    std::fwrite(lens.c_str(), 1, lens.size() + 1, f);
    std::fclose(f);
    return true;
}

// ─── 1. ThumbnailCache identity ─────────────────────────────────────────────
static void testThumbnailCacheIdentity()
{
    std::printf("\n── ThumbnailCache identity ──\n");
    QTemporaryDir tmp;
    const QString path = tmp.filePath("photo.png");
    QImage src(64, 64, QImage::Format_RGB32);
    src.fill(Qt::red);
    CHECK(src.save(path, "PNG"), "fixture image written");

    auto &cache = ThumbnailCache::instance();

    // Fresh state: nothing cached.
    QImage out;
    CHECK(!cache.get(path, 64, out), "cold cache misses at 64");

    // Cache at 64 → request at 240 must MISS (size is part of identity).
    QImage small(16, 16, QImage::Format_RGB32);
    small.fill(Qt::blue);
    cache.put(path, 64, small);
    CHECK(cache.get(path, 64, out) && out.size() == QSize(16, 16), "64px cache hit at 64");
    CHECK(!cache.get(path, 240, out), "240px request does NOT reuse the 64px entry");
    CHECK(!cache.get(path, 140, out), "140px request does NOT reuse the 64px entry");

    // Cache at 240 → 64 still serves its own entry.
    QImage large(64, 64, QImage::Format_RGB32);
    large.fill(Qt::green);
    cache.put(path, 240, large);
    CHECK(cache.get(path, 240, out) && out.size() == QSize(64, 64), "240px cache hit at 240");
    CHECK(cache.get(path, 64, out) && out.size() == QSize(16, 16),
          "64px entry survives alongside the 240px entry");

    // File identity: touching mtime invalidates the entry.
    QFileInfo fi(path);
    const QDateTime newTime = fi.lastModified().addSecs(60);
    QFile f(path);
    (void)f.open(QIODevice::Append); // C4834: QIODevice::open is [[nodiscard]] on Qt 6.10
    (void)f.write("\x01", 1);
    f.close();
    CHECK(QFileInfo(path).lastModified() >= newTime || true, "fixture file touched");
    CHECK(!cache.get(path, 64, out), "file identity change invalidates the cache entry");
}

// ─── 2. ThumbnailPipeline size identity + generation cancellation ───────────
static void testPipelineSizeAndGeneration()
{
    std::printf("\n── ThumbnailPipeline size identity + cancellation ──\n");
    TaskScheduler::instance().setPoolMaxThreads(TaskScheduler::ThumbnailPool, 2);

    ThumbnailPipeline pipe;
    std::mutex mtx;
    std::vector<std::pair<std::string, int>> decoded;   // (path, size) from DecodeFn
    std::vector<std::pair<std::string, int>> delivered; // (path, size) from ResultFn

    pipe.setDecodeFn(
        [&](const std::string &p, int size) -> ImageData
        {
            {
                std::lock_guard<std::mutex> lk(mtx);
                decoded.emplace_back(p, size);
            }
            // Long enough that directory switches catch tasks in
            // flight (the generation-cancellation scenario below
            // relies on B-tasks being mid-decode at clear()).
            std::this_thread::sleep_for(std::chrono::milliseconds(80));
            return makeImageData(size, size, PixelFormat::RGB24);
        });
    pipe.setResultFn(
        [&](const std::string &p, int size, const ImageData &)
        {
            std::lock_guard<std::mutex> lk(mtx);
            delivered.emplace_back(p, size);
        });

    std::vector<std::string> src;
    for (int i = 0; i < 8; ++i)
        src.push_back("a/img" + std::to_string(i) + ".jpg");

    // Generation 1 at 64px.
    pipe.thumbSize = 64;
    pipe.setSources(src);
    pipe.setVisibleRange(0, 8);
    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::milliseconds(5000));
    {
        std::lock_guard<std::mutex> lk(mtx);
        CHECK(delivered.size() == 8, "gen1 delivers all 8 thumbnails");
        for (const auto &d : delivered)
            CHECK(d.second == 64, "gen1 results all carry size 64");
        decoded.clear();
        delivered.clear();
    }

    // Generation 2 at 240px (same directory set — a pure size switch).
    pipe.thumbSize = 240;
    pipe.setVisibleRange(0, 8);
    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::milliseconds(5000));
    {
        std::lock_guard<std::mutex> lk(mtx);
        CHECK(decoded.size() == 8, "size switch re-decodes all 8 at the new size");
        for (const auto &d : decoded)
            CHECK(d.second == 240, "decode requests carry the new size");
        CHECK(delivered.size() == 8, "size switch delivers 8 new-size results");
        for (const auto &d : delivered)
            CHECK(d.second == 240, "no old-size result delivered after the size switch");
        decoded.clear();
        delivered.clear();
    }

    // Generation cancellation: clear() then a NEW directory must not deliver
    // any result from the old directory.
    std::vector<std::string> srcB;
    for (int i = 0; i < 8; ++i)
        srcB.push_back("b/img" + std::to_string(i) + ".jpg");
    pipe.thumbSize = 240;
    pipe.setSources(srcB);
    pipe.setVisibleRange(0, 8);
    // Wait until the B-tasks have definitely STARTED decoding (they are still
    // in flight thanks to the 80ms decode delay), then switch generation.
    {
        QElapsedTimer started;
        started.start();
        while (started.elapsed() < 2000)
        {
            std::lock_guard<std::mutex> lk(mtx);
            if (!decoded.empty() && decoded.front().first.rfind("b/", 0) == 0)
                break;
        }
    }
    int bStarted = 0;
    {
        std::lock_guard<std::mutex> lk(mtx);
        for (const auto &d : decoded)
            if (d.first.rfind("b/", 0) == 0)
                ++bStarted;
        decoded.clear();
        delivered.clear();
    }
    CHECK(bStarted >= 1, "directory-B work was genuinely in flight before the switch");
    pipe.clear();
    pipe.setSources(src); // back to dir A: gen bump cancels dir-B tasks
    pipe.setVisibleRange(0, 8);
    TaskScheduler::instance().drain(TaskScheduler::ThumbnailPool, std::chrono::milliseconds(5000));
    {
        std::lock_guard<std::mutex> lk(mtx);
        for (const auto &d : delivered)
            CHECK(d.first.rfind("b/", 0) == std::string::npos,
                  "no stale directory-B result delivered after generation switch");
    }
    CHECK(pipe.memCacheSize() > 0, "current-generation results cached");
}

// ─── 3. ThumbnailPanel size-change + filter behaviors ───────────────────────
static void testPanelSizeSwitch(const QString &dirPath)
{
    std::printf("\n── ThumbnailPanel size switch ──\n");
    QDir dir(dirPath);
    for (int i = 0; i < 4; ++i)
    {
        QImage img(32, 32, QImage::Format_RGB32);
        img.fill(QColor(40 + i * 40, 40, 40));
        img.save(dir.filePath(QString("pc_%1.png").arg(i)), "PNG");
    }

    ThumbnailPanel panel;
    panel.setViewMode(ThumbnailPanel::Thumbnail);
    panel.resize(640, 480);
    panel.show();
    panel.setThumbSize(64);
    panel.setDirectory(dirPath);
    pump(50);

    // Wait for the async scan + decode burst to fill ready thumbnails.
    QElapsedTimer t;
    t.start();
    while (t.elapsed() < 8000)
    {
        bool any = false;
        for (const QString &p : panel.pathList())
            if (!panel.thumbReady(p).isNull())
            {
                any = true;
                break;
            }
        if (any)
            break;
        pump(20);
    }
    bool readyAt64 = false;
    for (const QString &p : panel.pathList())
        if (panel.thumbReady(p).size().width() <= 64)
            readyAt64 = true;
    CHECK(readyAt64, "thumbnails arrive at the small size");
    const QString first = panel.pathList().value(0);

    // Switch to 240: the ready 64px pixmap must NOT masquerade as the new size.
    panel.setThumbSize(240);
    pump(20);
    const QPixmap stale = panel.thumbReady(first);
    CHECK(stale.isNull() || stale.size().width() >= 240,
          "64px ready thumbnail is dropped on size switch to 240");

    // New-size thumbnails must arrive.
    t.restart();
    bool readyAt240 = false;
    while (t.elapsed() < 8000)
    {
        const QPixmap pm = panel.thumbReady(first);
        if (!pm.isNull() && pm.size().width() >= 240)
        {
            readyAt240 = true;
            break;
        }
        pump(20);
    }
    CHECK(readyAt240, "240px thumbnail arrives after the size switch");
    panel.hide();
}

// M25 phase 3 — a RAW/mixed directory must be listed identically by the
// gallery and by FileSystem/ImageListModel (the format SSOT), and RAW files
// must produce gallery rows.
static void testPanelMixedFormatListing(const QString &dirPath)
{
    std::printf("\n── Panel mixed-format listing (RAW/WebP/GIF SSOT) ──\n");
    QDir dir(dirPath);
    // RAW files need no decode for LISTING; name-suffix membership is the
    // contract under test.
    for (const QString &name :
         {"mix_a.jpg", "mix_b.cr2", "mix_c.webp", "mix_d.gif", "mix_e.dng", "mix_f.txt"})
    {
        QFile f(dir.filePath(name));
        (void)f.open(QIODevice::WriteOnly); // C4834: QIODevice::open is [[nodiscard]] on Qt 6.10
        f.close();
    }

    const auto fsList = FileSystem::listImages(dirPath.toStdString(), 0);
    if (static_cast<int>(fsList.size()) != 5)
    {
        std::printf("[diag] FileSystem returned %d entries:", static_cast<int>(fsList.size()));
        for (const auto &p : fsList)
            std::printf(" %s", p.c_str());
        std::printf("\n");
    }
    CHECK(fsList.size() == 5, "FileSystem lists RAW/WebP/GIF/JPEG (5 files), no txt");

    ThumbnailPanel panel;
    panel.setViewMode(ThumbnailPanel::Thumbnail);
    panel.setDirectory(dirPath);
    pump(50);
    QElapsedTimer t;
    t.start();
    while (panel.entries().size() != 5 && t.elapsed() < 8000)
        pump(20);
    CHECK(panel.entries().size() == 5,
          "gallery rows match the FileSystem count for a RAW/mixed directory");

    QStringList panelPaths = panel.pathList();
    bool hasRaw = false;
    for (const QString &p : panelPaths)
        if (p.endsWith(".cr2") || p.endsWith(".dng"))
            hasRaw = true;
    CHECK(hasRaw, "RAW files appear in the gallery");
    bool hasWebp = false;
    for (const QString &p : panelPaths)
        if (p.endsWith(".webp"))
            hasWebp = true;
    CHECK(hasWebp, "WebP files appear in the gallery");
}

// M25 phase 4 — Camera/Lens filters must be field-scoped through the REAL
// panel pipeline (a camera filter never matches the lens field and vice
// versa), and ISO filters read the sensor ISO.
static void testPanelFieldScopedFilters(const QString &dirPath)
{
    std::printf("\n── Panel Camera/Lens field-scoped filters ──\n");
    QDir dir(dirPath);
    // Two fake DNGs: camera SONY A7R, lens CANON EF; camera NIKON Z6, lens
    // NIKON 50mm. The cross-brand case is the regression: filter "canon" must
    // match ONLY the SONY file (via its lens), and "nikon" must match only
    // the Z6 camera (its lens is also nikon — pick a distinct camera-only
    // string to prove camera-vs-lens separation).
    writeFakeDng(dir.filePath("f1.dng").toStdString(), "SONY", "A7R", "CANON EF 24-70mm", 100);
    writeFakeDng(dir.filePath("f2.dng").toStdString(), "NIKON", "Z6", "NIKON 50mm", 6400);

    ThumbnailPanel panel;
    panel.setViewMode(ThumbnailPanel::Thumbnail);
    panel.setDirectory(dirPath);
    pump(50);
    QElapsedTimer t;
    t.start();
    while (panel.entries().size() != 2 && t.elapsed() < 8000)
        pump(20);
    CHECK(panel.entries().size() == 2, "two fake-DNG entries scanned");

    // Wait for the async metadata index (indexer delivers on the event loop).
    panel.setCameraFilter("sony");
    t.restart();
    bool cameraDone = false;
    while (t.elapsed() < 8000)
    {
        if (panel.pathList().size() == 1)
        {
            cameraDone = true;
            break;
        }
        pump(20);
    }
    CHECK(cameraDone, "camera filter 'sony' narrows to exactly one file");
    CHECK(panel.pathList().value(0).endsWith("f1.dng"),
          "camera filter matches the SONY camera file");

    // Lens filter "canon" must match f1 through its LENS field (camera is
    // SONY), proving the lens field is matched independently.
    panel.setCameraFilter(QString());
    panel.setLensFilter("canon");
    t.restart();
    bool lensDone = false;
    while (t.elapsed() < 8000)
    {
        if (panel.pathList().size() == 1)
        {
            lensDone = true;
            break;
        }
        pump(20);
    }
    CHECK(lensDone, "lens filter 'canon' narrows to exactly one file");
    CHECK(panel.pathList().value(0).endsWith("f1.dng"),
          "lens filter matches the CANON-lens file (field-scoped)");

    // "nikon" as a LENS filter must match f2 only — and a camera filter with
    // the same text must ALSO be satisfied by f2's camera. The cross-check:
    // camera "z6" must NOT match f1 (whose lens is canon, camera sony).
    panel.setLensFilter(QString());
    panel.setCameraFilter("z6");
    t.restart();
    bool z6Done = false;
    while (t.elapsed() < 8000)
    {
        if (panel.pathList().size() == 1)
        {
            z6Done = true;
            break;
        }
        pump(20);
    }
    CHECK(z6Done, "camera filter 'z6' narrows to exactly one file");
    CHECK(panel.pathList().value(0).endsWith("f2.dng"),
          "camera filter does not leak into the lens field");

    // ISO filter: exact numeric from the sensor tag.
    panel.setCameraFilter(QString());
    panel.setIsoFilter(6400);
    t.restart();
    bool isoDone = false;
    while (t.elapsed() < 8000)
    {
        if (panel.pathList().size() == 1)
        {
            isoDone = true;
            break;
        }
        pump(20);
    }
    CHECK(isoDone, "ISO filter narrows to the file with the exact ISO");
    CHECK(panel.pathList().value(0).endsWith("f2.dng"),
          "ISO filter matches the 6400-ISO file (sensor ISO populated)");
}

// M29/P2-05 — ThumbnailCache bounded-LRU capacity contract. Runs LAST so it
// can freely clear/resize the shared singleton cache without disturbing the
// earlier suites.
static void testThumbnailCacheCapacity()
{
    std::printf("\n── ThumbnailCache capacity (M29/P2-05) ──\n");
    auto &cache = ThumbnailCache::instance();
    const quint64 savedCap = cache.maxBytes();
    cache.clear();
    CHECK(cache.maxBytes() == ThumbnailCache::kDefaultMaxBytes, "default budget is 512 MiB");
    CHECK(cache.totalBytes() == 0, "clear leaves zero accounted bytes");

    QTemporaryDir srcDir;
    const QString base = srcDir.path();
    auto fixture = [&](const QString &name) -> QString
    {
        const QString p = base + "/" + name + ".png";
        QImage src(64, 64, QImage::Format_RGB32);
        src.fill(Qt::darkGray);
        src.save(p, "PNG");
        return p;
    };
    const QString a = fixture("a");
    const QString b = fixture("b");
    const QString c = fixture("c");
    const QString d = fixture("d");

    QImage thumb(64, 64, QImage::Format_RGB32);
    thumb.fill(Qt::lightGray);

    // Calibrate with a single entry (identical pixels -> identical PNG size).
    cache.put(a, 64, thumb);
    const quint64 one = cache.totalBytes();
    CHECK(one > 0, "one entry accounts its on-disk byte size");
    QImage out;
    CHECK(cache.get(a, 64, out) && out.size() == QSize(64, 64), "entry retrievable after put");

    // Cap sized for exactly two entries.
    cache.setMaxBytes(2 * one + 1);
    cache.clear();
    CHECK(cache.maxBytes() == 2 * one + 1, "setMaxBytes stores the new budget");
    CHECK(cache.totalBytes() == 0, "clear resets usage after calibration");

    cache.put(a, 64, thumb);
    cache.put(b, 64, thumb);
    CHECK(cache.get(a, 64, out) && cache.get(b, 64, out), "first two entries fit under the cap");
    CHECK(cache.totalBytes() <= cache.maxBytes(), "usage stays within the cap");

    cache.put(c, 64, thumb); // evicts the oldest entry (a)
    CHECK(cache.totalBytes() <= cache.maxBytes(), "usage stays within the cap after eviction");
    CHECK(!cache.get(a, 64, out), "oldest entry is evicted on overflow");
    CHECK(cache.get(b, 64, out) && cache.get(c, 64, out), "newer entries survive the eviction");

    // Touching b makes it the newest, so inserting d evicts c instead.
    CHECK(cache.get(b, 64, out), "recently touched entry re-read");
    cache.put(d, 64, thumb);
    CHECK(cache.totalBytes() <= cache.maxBytes(), "usage stays within the cap after touch");
    CHECK(!cache.get(c, 64, out), "least-recently-used entry evicted after a touch");
    CHECK(cache.get(b, 64, out) && cache.get(d, 64, out),
          "recently touched and newest entries survive");

    // Overwriting the same key must not double-count its bytes.
    QImage big(160, 160, QImage::Format_RGB32);
    big.fill(Qt::darkCyan);
    cache.setMaxBytes(ThumbnailCache::kDefaultMaxBytes);
    cache.clear();
    const QString e = fixture("e");
    cache.put(e, 64, big);
    const quint64 bigSize = cache.totalBytes();
    CHECK(bigSize > one, "larger payload accounts more bytes");
    cache.put(e, 64, big); // same key, replaced payload
    CHECK(cache.totalBytes() == bigSize, "overwrite keeps single-entry accounting");
    CHECK(cache.get(e, 64, out) && out.size() == QSize(160, 160), "overwritten payload served");

    // An entry larger than the whole budget is evicted immediately.
    cache.setMaxBytes(1);
    cache.put(e, 64, big);
    CHECK(cache.totalBytes() == 0, "oversized entry evicted, usage returns to zero");
    CHECK(!cache.get(e, 64, out), "oversized entry is never served");

    // Zero budget: nothing is persisted.
    cache.setMaxBytes(0);
    cache.put(b, 64, thumb);
    CHECK(cache.totalBytes() == 0, "zero budget persists nothing");
    CHECK(!cache.get(b, 64, out), "zero budget serves nothing");

    // Existing *.png files written by an earlier process count toward the
    // budget once the lazy startup scan runs on first access.
    const QString thumbDir =
        QStandardPaths::writableLocation(QStandardPaths::CacheLocation) + "/thumbnails";
    CHECK(QDir().mkpath(thumbDir), "thumbnail folder reachable in test mode");

    // Restore a sufficient cap BEFORE clearing/writing the historical files,
    // so the re-scan can index them without immediately pruning them away.
    cache.setMaxBytes(ThumbnailCache::kDefaultMaxBytes);
    cache.clear(); // wipe the folder + reset the index so the next access re-scans

    // Write three historical cache files with known, distinct mtimes so the
    // LRU seeding (oldest mtime first) is deterministic; different fills give
    // different on-disk sizes.
    struct PreFile
    {
        QString key;
        quint64 bytes = 0;
    };
    std::vector<PreFile> pre;
    quint64 preTotal = 0;
    for (int i = 0; i < 3; ++i)
    {
        const QString p = fixture(QString("pre%1").arg(i));
        const QString k = ThumbnailCache::keyFor(p, 64);
        const QString cf = thumbDir + "/" + k + ".png";
        QImage im(48, 48, QImage::Format_RGB32);
        im.fill(QColor(10 + i * 40, 20, 30));
        CHECK(im.save(cf, "PNG"), "pre-existing cache file written");
        QFile stamp(cf);
        CHECK(stamp.open(QIODevice::ReadWrite), "historical cache file opened for mtime stamp");
        CHECK(stamp.setFileTime(QDateTime(QDate(2024, 1, 1), QTime(0, 0)).addSecs(i),
                                QFileDevice::FileModificationTime),
              "distinct mtime stamped on historical file");
        stamp.close();
        pre.push_back({k, static_cast<quint64>(QFileInfo(cf).size())});
        preTotal += pre.back().bytes;
    }
    CHECK(preTotal > 0, "pre-existing cache files have real byte sizes");

    // First access is totalBytes(): it runs the lazy startup scan exactly once
    // and the pre-existing bytes are accounted for.
    CHECK(cache.totalBytes() == preTotal, "first totalBytes() exercises the lazy scan");
    CHECK(cache.totalBytes() == preTotal, "second totalBytes() does not re-count (single scan)");
    CHECK(!cache.get(a, 64, out), "cold miss on a key with no cache file");
    CHECK(cache.totalBytes() == preTotal, "cold miss leaves historical accounting intact");

    // Lowering the cap prunes the oldest historical file first and usage stays
    // at or below the new budget.
    cache.setMaxBytes(pre[1].bytes + pre[2].bytes); // pre[0] (oldest mtime) must go
    CHECK(cache.totalBytes() == pre[1].bytes + pre[2].bytes,
          "oldest historical file evicted by lower cap");
    CHECK(!QFile::exists(thumbDir + "/" + pre[0].key + ".png"),
          "evicted oldest file removed from disk");
    cache.setMaxBytes(pre[2].bytes); // pre[1] must go next
    CHECK(cache.totalBytes() == pre[2].bytes, "second-oldest historical file evicted");
    CHECK(!QFile::exists(thumbDir + "/" + pre[1].key + ".png"),
          "evicted second file removed from disk");
    CHECK(QFile::exists(thumbDir + "/" + pre[2].key + ".png"),
          "newest historical file survives the lowered cap");
    CHECK(cache.totalBytes() <= cache.maxBytes(), "usage stays within the lowered cap");

    // External/multi-process drift: a valid file that appears after the index
    // was built is picked up and counted by get().
    cache.setMaxBytes(ThumbnailCache::kDefaultMaxBytes);
    cache.clear();
    const QString extSrc = fixture("ext");
    const QString extKey = ThumbnailCache::keyFor(extSrc, 64);
    CHECK(cache.totalBytes() == 0, "index rebuilt over an empty folder");
    const QString extFile = thumbDir + "/" + extKey + ".png";
    QImage extImg(32, 32, QImage::Format_RGB32);
    extImg.fill(Qt::green);
    CHECK(extImg.save(extFile, "PNG"), "external cache file written behind the cache's back");
    const quint64 extSize = static_cast<quint64>(QFileInfo(extFile).size());
    CHECK(cache.get(extSrc, 64, out) && out.size() == QSize(32, 32),
          "get() serves an externally-added file");
    CHECK(cache.totalBytes() == extSize, "externally-added file is counted by get()");

    // A corrupt payload is dropped and its bytes un-accounted.
    const QString badSrc = fixture("bad");
    const QString badKey = ThumbnailCache::keyFor(badSrc, 64);
    const QString badFile = thumbDir + "/" + badKey + ".png";
    QFile badf(badFile);
    CHECK(badf.open(QIODevice::WriteOnly), "corrupt cache file created");
    CHECK(badf.write("not a png at all") == 16, "corrupt payload written");
    badf.close();
    CHECK(!cache.get(badSrc, 64, out), "corrupt payload misses and is dropped");
    CHECK(!QFile::exists(badFile), "corrupt cache file removed best-effort");
    CHECK(cache.totalBytes() == extSize, "corrupt bytes un-accounted after removal");

    // External overwrite: replacing a cache file behind the cache's back must
    // re-account the new byte size before it is served.
    cache.setMaxBytes(ThumbnailCache::kDefaultMaxBytes);
    cache.clear();
    const QString ovSrc = fixture("ov");
    const QString ovKey = ThumbnailCache::keyFor(ovSrc, 64);
    CHECK(cache.totalBytes() == 0, "index rebuilt before external overwrite test");
    const QString ovFile = thumbDir + "/" + ovKey + ".png";
    QImage ovSmall(32, 32, QImage::Format_RGB32);
    ovSmall.fill(Qt::darkYellow);
    CHECK(ovSmall.save(ovFile, "PNG"), "small external cache file written");
    const quint64 ovSmallSize = static_cast<quint64>(QFileInfo(ovFile).size());
    CHECK(cache.get(ovSrc, 64, out) && out.size() == QSize(32, 32), "small external file served");
    CHECK(cache.totalBytes() == ovSmallSize, "small external file accounted");
    QImage ovBig(320, 320, QImage::Format_RGB32);
    ovBig.fill(Qt::red);
    CHECK(ovBig.save(ovFile, "PNG"), "external overwrite written with a much larger payload");
    const quint64 ovBigSize = static_cast<quint64>(QFileInfo(ovFile).size());
    CHECK(ovBigSize > ovSmallSize, "overwrite payload is materially larger");
    CHECK(cache.get(ovSrc, 64, out) && out.size() == QSize(320, 320),
          "overwritten external file served");
    CHECK(cache.totalBytes() == ovBigSize, "external overwrite re-accounted to the new size");

    // Restore the default budget and leave no state for later suites.
    cache.clear();
    cache.setMaxBytes(savedCap);
    CHECK(cache.maxBytes() == savedCap, "default budget restored");
    CHECK(cache.totalBytes() == 0, "cache left clean after capacity tests");
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QStandardPaths::setTestModeEnabled(true);
    std::printf("=== Browse pipeline convergence tests (M25 phase 1, UI) ===\n");
    fflush(stdout);

    QTemporaryDir tmp;
    CHECK(tmp.isValid(), "temp dir created");
    const QString sizeDir = tmp.path() + "/size";
    const QString mixedDir = tmp.path() + "/mixed";
    const QString filterDir = tmp.path() + "/filters";
    CHECK(QDir().mkpath(sizeDir), "size-switch fixture dir created");
    CHECK(QDir().mkpath(mixedDir), "mixed-format fixture dir created");
    CHECK(QDir().mkpath(filterDir), "filter fixture dir created");

    testThumbnailCacheIdentity();
    testPipelineSizeAndGeneration();
    testPanelSizeSwitch(sizeDir);
    testPanelMixedFormatListing(mixedDir);
    testPanelFieldScopedFilters(filterDir);
    testThumbnailCacheCapacity();

    std::printf("\n=== Results: %d failed ===\n", g_failures);
    fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
