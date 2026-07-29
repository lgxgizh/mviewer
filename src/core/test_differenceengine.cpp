// DifferenceEngine unit tests — pixel diff / heatmap / threshold.
#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageBuffer.h"
#include <QApplication>
#include <iostream>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << "\n";                                                  \
            ++g_fail;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << "\n";                                                  \
        }                                                                                          \
    } while (0)

static ImageData makeSolidRgb(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    const size_t sz = static_cast<size_t>(w) * h * 3;
    auto buf = std::make_shared<std::vector<uint8_t>>(sz);
    for (size_t i = 0; i < sz; i += 3)
    {
        (*buf)[i + 0] = r;
        (*buf)[i + 1] = g;
        (*buf)[i + 2] = b;
    }
    ImageData d;
    d.buffer = std::move(buf);
    d.width = w;
    d.height = h;
    d.format = PixelFormat::RGB24;
    return d;
}

static bool isAllBlack(const ImageData &img)
{
    if (img.isNull())
        return false;
    for (auto v : *img.buffer)
        if (v != 0)
            return false;
    return true;
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);

    // identical images produce zero diff
    {
        auto a = makeSolidRgb(16, 16, 128, 64, 32);
        auto diff = DifferenceEngine::differenceMap(a, a);
        CHECK(!diff.isNull(), "diff identical non-null");
        CHECK(isAllBlack(diff), "diff identical all black");
    }

    // different images produce non-zero diff
    {
        auto a = makeSolidRgb(16, 16, 255, 0, 0);
        auto b = makeSolidRgb(16, 16, 0, 0, 0);
        auto diff = DifferenceEngine::differenceMap(a, b);
        CHECK(!diff.isNull(), "diff different non-null");
        CHECK(!isAllBlack(diff), "diff different not all black");
    }

    // threshold reduces highlighted pixels
    {
        auto a = makeSolidRgb(16, 16, 128, 128, 128);
        auto b = makeSolidRgb(16, 16, 128, 128, 128);
        (*a.buffer)[0] = 255;
        (*b.buffer)[0] = 0;
        auto diffRaw = DifferenceEngine::differenceMap(a, b);
        auto diffThresh = DifferenceEngine::differenceMap(a, b, 200);
        int rawNZ = 0, threshNZ = 0;
        for (auto v : *diffRaw.buffer)
            if (v > 0)
                ++rawNZ;
        for (auto v : *diffThresh.buffer)
            if (v > 0)
                ++threshNZ;
        CHECK(threshNZ < rawNZ || rawNZ == 0, "threshold reduces highlighted pixels");
    }

    // heatMap
    {
        auto buf = std::make_shared<std::vector<uint8_t>>(64, uint8_t(100));
        ImageData gray;
        gray.buffer = buf;
        gray.width = 8;
        gray.height = 8;
        gray.format = PixelFormat::Grayscale8;
        auto heat = DifferenceEngine::heatMap(gray);
        CHECK(!heat.isNull(), "heatMap non-null");
    }

    // applyThreshold
    {
        auto buf = std::make_shared<std::vector<uint8_t>>(256, uint8_t(50));
        ImageData gray;
        gray.buffer = buf;
        gray.width = 16;
        gray.height = 16;
        gray.format = PixelFormat::Grayscale8;
        auto thresh = DifferenceEngine::applyThreshold(gray, 100);
        CHECK(!thresh.isNull(), "applyThreshold non-null");
        CHECK(isAllBlack(thresh), "threshold > pixel all black");
    }

    // A-4.6: highlightMap — diffs red, similar gray
    {
        auto a = makeSolidRgb(8, 8, 100, 100, 100);
        auto b = makeSolidRgb(8, 8, 100, 100, 100);
        // One bright-diff pixel at (0,0)
        (*a.buffer)[0] = 255;
        (*a.buffer)[1] = 255;
        (*a.buffer)[2] = 255;
        (*b.buffer)[0] = 0;
        (*b.buffer)[1] = 0;
        (*b.buffer)[2] = 0;
        auto diff = DifferenceEngine::differenceMap(a, b);
        auto hl = DifferenceEngine::highlightMap(diff, a, /*threshold=*/10);
        CHECK(!hl.isNull(), "highlightMap non-null");
        CHECK(hl.format == PixelFormat::RGB24, "highlightMap RGB24");
        // Diff pixel should be red-dominant
        CHECK((*hl.buffer)[0] > (*hl.buffer)[1] && (*hl.buffer)[0] > (*hl.buffer)[2],
              "highlightMap diff pixel is red");
        // A similar pixel (e.g. index 3) should be gray (R==G==B)
        const size_t off = 3 * 3;
        CHECK((*hl.buffer)[off] == (*hl.buffer)[off + 1] &&
                  (*hl.buffer)[off] == (*hl.buffer)[off + 2],
              "highlightMap similar pixel is gray");
    }

    // M23: computeStats — full image
    {
        auto a = makeSolidRgb(10, 10, 100, 100, 100);
        auto b = makeSolidRgb(10, 10, 100, 100, 100);
        // Two diff pixels: (0,0) strong, (1,0) weak.
        // Gray diff = (dr+dg+db)/3 → strong ≈ 33, weak ≈ 1.
        (*a.buffer)[0] = 200; // R at (0,0): |200-100| = 100 → gray ≈ 33
        (*a.buffer)[3] = 105; // R at (1,0): |105-100| = 5   → gray ≈ 1
        auto diff = DifferenceEngine::differenceMap(a, b);
        const auto st = DifferenceEngine::computeStats(diff);
        CHECK(st.totalPixels == 100, "stats total = 100");
        CHECK(st.diffPixels == 2, "stats diffPixels = 2 (threshold 0)");
        CHECK(st.maxDiff > 0 && st.maxDiff <= 255, "stats maxDiff in range");
        CHECK(st.meanDiff > 0.0, "stats meanDiff > 0");
        CHECK(st.diffRatio > 0.019 && st.diffRatio < 0.021, "stats diffRatio ≈ 2%");

        // Threshold filters out the weak diff pixel (gray ≈ 1 < 20 ≤ 33)
        const auto st2 = DifferenceEngine::computeStats(diff, 20);
        CHECK(st2.diffPixels == 1, "stats threshold=20 keeps only strong pixel");
    }

    // M23: computeStats — ROI clipping
    {
        auto a = makeSolidRgb(10, 10, 100, 100, 100);
        auto b = makeSolidRgb(10, 10, 100, 100, 100);
        (*a.buffer)[0] = 255; // diff only at (0,0)
        auto diff = DifferenceEngine::differenceMap(a, b);
        // ROI covering (0,0)
        const auto in = DifferenceEngine::computeStats(diff, 0, 0, 0, 2, 2);
        CHECK(in.totalPixels == 4 && in.diffPixels == 1, "ROI stats include diff pixel");
        // ROI away from (0,0)
        const auto out = DifferenceEngine::computeStats(diff, 0, 5, 5, 3, 3);
        CHECK(out.totalPixels == 9 && out.diffPixels == 0, "ROI stats exclude diff pixel");
        // ROI partially outside is clipped
        const auto clip = DifferenceEngine::computeStats(diff, 0, 8, 8, 10, 10);
        CHECK(clip.totalPixels == 4, "ROI clipped to bounds");
        // Degenerate / fully-outside ROI
        const auto deg = DifferenceEngine::computeStats(diff, 0, 0, 0, 0, 0);
        CHECK(deg.totalPixels == 0 && deg.diffRatio == 0.0, "degenerate ROI = empty stats");
        const auto off = DifferenceEngine::computeStats(diff, 0, 20, 20, 4, 4);
        CHECK(off.totalPixels == 0, "outside ROI = empty stats");
        // Null input
        const auto nul = DifferenceEngine::computeStats(ImageData{});
        CHECK(nul.totalPixels == 0, "null input = empty stats");
    }

    std::cout << "\nDifferenceEngine: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
