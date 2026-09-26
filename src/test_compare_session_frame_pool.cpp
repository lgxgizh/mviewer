#include "compare_session_frame_pool.h"

#include "core/image/ImageFrame.h"

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <cstdio>
#include <cstring>
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

    std::printf("=== Compare session frame pool tests: %s ===\n", failures == 0 ? "PASS" : "FAIL");
    return failures == 0 ? 0 : 1;
}
