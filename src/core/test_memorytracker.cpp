#include "core/perf/MemoryTracker.h"

#include "core/cache/CacheManager.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <cassert>
#include <iostream>
#include <utility>
#include <vector>

// Structural tests for MemoryTracker (docs/rfc/M10_PERFORMANCE_ENGINEERING.md).
// These assert deterministic ledger behavior — NOT wall-clock budgets.

static int g_failures = 0;
#define CHECK(cond)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " #cond " @ " << __LINE__ << "\n";                                 \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (0)

using namespace mviewer::perf;

static void test_samplesCacheManager()
{
    auto &cm = CacheManager::instance();
    cm.clearMemory();
    auto &mt = MemoryTracker::instance();
    mt.reset();

    // Put a known image into the Viewer (FullImage) pool.
    ImageData img = makeImageData(256, 256, PixelFormat::RGB24); // 196608 bytes
    const size_t bytes = img.byteSize();
    cm.putMemory(CacheLevel::FullImage, "memtracker_test_key", img);

    const MemorySnapshot s = mt.sample();
    CHECK(s.cacheTotalBytes == cm.memoryUsageBytes());
    CHECK(s.cacheTotalBytes >= bytes);
    CHECK(s.cacheByLevel[static_cast<int>(MemLevel::Viewer)] >= bytes);
    CHECK(s.liveImageFrames >= 0); // structural, >=0 always
}

static void test_liveFrameCounter()
{
    auto &mt = MemoryTracker::instance();
    mt.reset();
    const size_t before = mt.sample().liveImageFrames;

    // Construct and destroy K frames; counter must track exactly.
    const int K = 16;
    {
        std::vector<ImageFrame> frames;
        frames.reserve(K);
        for (int i = 0; i < K; ++i)
            frames.emplace_back(); // default ctor -> notifyFrameCreated
    } // all dtors -> notifyFrameDestroyed

    const size_t after = mt.sample().liveImageFrames;
    CHECK(after == before); // all destroyed -> back to baseline
    CHECK(mt.peak().liveImageFrames >= before + static_cast<size_t>(K));
}

static void test_copyMoveFrameAccounting()
{
    auto &mt = MemoryTracker::instance();
    const ImageFrame sentinel;
    const size_t before = mt.sample().liveImageFrames;

    mviewer::domain::ImageMetadata meta;
    meta.filePath = "frame-account.png";
    ImageData pixels = makeImageData(2, 2, PixelFormat::RGB24);
    {
        ImageFrame a(meta, pixels);
        a.addTag("keep");
        CHECK(mt.sample().liveImageFrames == before + 1);
        ImageFrame b = a;
        CHECK(mt.sample().liveImageFrames == before + 2);
        CHECK(b.metadata().filePath == "frame-account.png" && b.hasTag("keep"));
        CHECK(b.pixels().buffer == a.pixels().buffer);
        ImageFrame c = std::move(b);
        CHECK(mt.sample().liveImageFrames == before + 2);
        CHECK(c.metadata().filePath == "frame-account.png" && c.hasTag("keep"));
        b = a; // reassign the moved-from frame: copy-assign re-activates its count
        CHECK(mt.sample().liveImageFrames == before + 3);
        CHECK(b.hasTag("keep"));
    }
    CHECK(mt.sample().liveImageFrames == before);

    {
        ImageFrame a(meta, pixels);
        ImageFrame b(meta, pixels);
        const size_t both = mt.sample().liveImageFrames;
        b = a;
        CHECK(mt.sample().liveImageFrames == both);
        a = std::move(b);
        CHECK(mt.sample().liveImageFrames == both - 1);
    }
    CHECK(mt.sample().liveImageFrames == before);

    {
        std::vector<ImageFrame> frames;
        frames.emplace_back(meta, pixels);
        for (int i = 0; i < 16; ++i)
        {
            ImageFrame extra = frames.back();
            frames.push_back(std::move(extra));
        }
        CHECK(mt.sample().liveImageFrames == before + frames.size());
    }
    CHECK(mt.sample().liveImageFrames == before);
}

static void test_externalAndPeak()
{
    auto &mt = MemoryTracker::instance();
    mt.reset();

    mt.addExternal(1024);
    MemorySnapshot s1 = mt.sample();
    CHECK(s1.externalBytes == 1024);
    CHECK(s1.peakBytes >= 1024);

    mt.removeExternal(512);
    MemorySnapshot s2 = mt.sample();
    CHECK(s2.externalBytes == 512);

    mt.removeExternal(size_t{10} * size_t{1024});
    CHECK(mt.sample().externalBytes == 0);
    mt.addExternal(50);
    mt.removeExternal(50);
    mt.removeExternal(1);
    CHECK(mt.sample().externalBytes == 0);

    mt.reset();
    CHECK(mt.peak().peakBytes == 0);
    CHECK(mt.sample().externalBytes == 0);
}

static void test_timelineRing()
{
    // M21: sample() appends to a ring buffer; reset() clears it.
    auto &mt = MemoryTracker::instance();
    mt.reset();
    CHECK(mt.timelineSize() == 0);

    for (int i = 0; i < 5; ++i)
        mt.sample();
    CHECK(mt.timelineSize() == 5);
    const auto hist = mt.timeline();
    CHECK(hist.size() == 5);
    // Timestamps are non-decreasing.
    for (size_t i = 1; i < hist.size(); ++i)
        CHECK(hist[i].timestampMs >= hist[i - 1].timestampMs);

    mt.reset();
    CHECK(mt.timelineSize() == 0);
}

int memorytracker_suite()
{
    test_samplesCacheManager();
    test_liveFrameCounter();
    test_copyMoveFrameAccounting();
    test_externalAndPeak();
    test_timelineRing();
    return g_failures;
}
