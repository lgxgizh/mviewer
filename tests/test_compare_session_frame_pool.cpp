#include "compare_session_frame_pool.h"

#include "core/image/ImageFrame.h"

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <cstdio>
#include <cstring>
#include <memory>
#include <string>

using mviewer::ui::CompareSessionFramePool;

static std::shared_ptr<ImageFrame> makeFrame(const std::string &path, int w, int h)
{
    ImageData pixels;
    pixels.width = w;
    pixels.height = h;
    pixels.format = PixelFormat::RGB24;
    pixels.buffer = std::make_shared<std::vector<uint8_t>>(static_cast<size_t>(w) * h * 3, 0);
    mviewer::domain::ImageMetadata meta;
    meta.filePath = path;
    meta.fileName = path;
    meta.width = w;
    meta.height = h;
    return std::make_shared<ImageFrame>(meta, pixels);
}

int main()
{
    int failures = 0;

    CompareSessionFramePool pool(/*maxEntries=*/3, /*maxBytes=*/1024ull * 1024ull);

    auto a = makeFrame("/a.png", 100, 100); // ~30KB
    auto b = makeFrame("/b.png", 100, 100);
    auto c = makeFrame("/c.png", 100, 100);
    auto d = makeFrame("/d.png", 100, 100);

    pool.put("/a.png", 0, a);
    pool.put("/b.png", 0, b);
    pool.put("/c.png", 0, c);
    if (pool.size() != 3)
    {
        std::printf("FAIL: expected 3 entries, got %zu\n", pool.size());
        ++failures;
    }
    else
        std::printf("PASS: filled to maxEntries\n");

    // Touch a so b becomes LRU victim on next put.
    if (!pool.tryGet("/a.png", 0))
    {
        std::printf("FAIL: tryGet a\n");
        ++failures;
    }
    else
        std::printf("PASS: tryGet a\n");

    pool.put("/d.png", 0, d);
    if (pool.tryGet("/b.png", 0))
    {
        std::printf("FAIL: b should be evicted\n");
        ++failures;
    }
    else
        std::printf("PASS: LRU eviction dropped b\n");

    if (!pool.tryGet("/a.png", 0) || !pool.tryGet("/c.png", 0) || !pool.tryGet("/d.png", 0))
    {
        std::printf("FAIL: a/c/d should remain\n");
        ++failures;
    }
    else
        std::printf("PASS: a/c/d retained\n");

    // Frame index keying (fresh pool so eviction cannot drop frame 0).
    {
        CompareSessionFramePool keyed(/*maxEntries=*/4, /*maxBytes=*/1024ull * 1024ull);
        auto a0 = makeFrame("/a.png", 100, 100);
        auto a1 = makeFrame("/a.png", 80, 80);
        keyed.put("/a.png", 0, a0);
        keyed.put("/a.png", 1, a1);
        if (!keyed.tryGet("/a.png", 1) || !keyed.tryGet("/a.png", 0))
        {
            std::printf("FAIL: path+frameIndex keying\n");
            ++failures;
        }
        else
            std::printf("PASS: path+frameIndex keying\n");
    }

    // Null pixels ignored
    mviewer::domain::ImageMetadata meta;
    meta.filePath = "/empty.png";
    auto empty = std::make_shared<ImageFrame>(meta, ImageData());
    const size_t before = pool.size();
    pool.put("/empty.png", 0, empty);
    if (pool.size() != before)
    {
        std::printf("FAIL: empty frame should be ignored\n");
        ++failures;
    }
    else
        std::printf("PASS: empty frame ignored\n");

    pool.clear();
    if (pool.size() != 0 || pool.bytes() != 0)
    {
        std::printf("FAIL: clear\n");
        ++failures;
    }
    else
        std::printf("PASS: clear\n");

    {
        CompareSessionFramePool held;
        auto only = makeFrame("/only.png", 32, 32);
        std::weak_ptr<ImageFrame> weak = only;
        held.put("/only.png", 0, only);
        only.reset();
        const bool retained = !weak.expired() && held.size() == 1 && held.bytes() > 0;
        held.clear();
        const bool released = held.size() == 0 && held.bytes() == 0 && weak.expired();
        if (!retained || !released)
        {
            std::printf("FAIL: clear drops the last session frame (retained=%d released=%d)\n",
                        retained ? 1 : 0, released ? 1 : 0);
            ++failures;
        }
        else
            std::printf("PASS: clear drops the last session frame\n");
    }

    {
        // Budget follows the loaded pair: only the pair on screen is kept.
        using P = CompareSessionFramePool;
        const size_t pair = 2ull * 4096ull * 3072ull * 3ull; // two 12.6 MP RGB24
        const size_t budget = P::pairBudgetBytes(pair);
        const bool sized = budget >= pair && budget < pair * 2;
        const bool floor = P::pairBudgetBytes(1024) == P::kMinBudgetBytes;
        const bool ceiling = P::pairBudgetBytes(1ull << 34) == P::kDefaultMaxBytes;
        if (!sized || !floor || !ceiling)
        {
            std::printf("FAIL: pairBudgetBytes (sized=%d floor=%d ceiling=%d)\n", sized, floor,
                        ceiling);
            ++failures;
        }
        else
            std::printf("PASS: pairBudgetBytes\n");

        P pairPool;
        const size_t one = P::frameBytes(makeFrame("/x.png", 64, 64));
        pairPool.setPairBudget(2, 2 * one);
        for (int i = 0; i < 6; ++i)
        {
            const std::string path = "/p" + std::to_string(i) + ".png";
            pairPool.put(path, 0, makeFrame(path, 64, 64));
        }
        const bool keepsCurrentPair = pairPool.size() == 2 && pairPool.tryGet("/p5.png", 0) &&
                                      pairPool.tryGet("/p4.png", 0) &&
                                      !pairPool.tryGet("/p3.png", 0);
        pairPool.setPairBudget(1, one);
        const bool shrinks = pairPool.size() == 1;
        if (!keepsCurrentPair || !shrinks)
        {
            std::printf("FAIL: setPairBudget (keepsCurrentPair=%d shrinks=%d)\n", keepsCurrentPair,
                        shrinks);
            ++failures;
        }
        else
            std::printf("PASS: setPairBudget keeps only the current pair\n");
    }

    std::printf("=== Compare session frame pool tests: %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
