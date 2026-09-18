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

    // amplify
    {
        auto buf = std::make_shared<std::vector<uint8_t>>(4, uint8_t(10));
        (*buf)[1] = 100;
        (*buf)[2] = 200;
        ImageData gray;
        gray.buffer = buf;
        gray.width = 2;
        gray.height = 2;
        gray.format = PixelFormat::Grayscale8;

        // Gain <= 1.0 identity
        auto id = DifferenceEngine::amplify(gray, 1.0);
        CHECK(!id.isNull() && (*id.buffer)[0] == 10 && (*id.buffer)[2] == 200,
              "amplify gain 1.0 preserves values");

        // Gain 2.0
        auto amp2 = DifferenceEngine::amplify(gray, 2.0);
        CHECK((*amp2.buffer)[0] == 20, "amplify 10*2 = 20");
        CHECK((*amp2.buffer)[1] == 200, "amplify 100*2 = 200");
        CHECK((*amp2.buffer)[2] == 255, "amplify 200*2 saturates to 255");

        // Null image
        ImageData nullImg;
        CHECK(DifferenceEngine::amplify(nullImg, 4.0).isNull(), "amplify null returns null");
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

        auto hlZero = DifferenceEngine::highlightMap(diff, a, /*threshold=*/0);
        CHECK((*hlZero.buffer)[off] == (*hlZero.buffer)[off + 1] &&
                  (*hlZero.buffer)[off] == (*hlZero.buffer)[off + 2],
              "highlightMap threshold zero keeps identical pixels gray");

        // Grayscale8 base test
        auto diffGrayBuf = std::make_shared<std::vector<uint8_t>>(16, uint8_t(0));
        (*diffGrayBuf)[0] = 50;
        ImageData diffGray;
        diffGray.buffer = diffGrayBuf;
        diffGray.width = 4;
        diffGray.height = 4;
        diffGray.format = PixelFormat::Grayscale8;

        auto baseGrayBuf = std::make_shared<std::vector<uint8_t>>(16, uint8_t(180));
        ImageData baseGray;
        baseGray.buffer = baseGrayBuf;
        baseGray.width = 4;
        baseGray.height = 4;
        baseGray.format = PixelFormat::Grayscale8;

        auto hlGray = DifferenceEngine::highlightMap(diffGray, baseGray, 10);
        CHECK(!hlGray.isNull() && hlGray.format == PixelFormat::RGB24,
              "highlightMap Grayscale8 base non-null");
        CHECK((*hlGray.buffer)[0] > (*hlGray.buffer)[1] &&
                  (*hlGray.buffer)[0] > (*hlGray.buffer)[2],
              "highlightMap Grayscale8 diff pixel is red");
        CHECK((*hlGray.buffer)[3] == 180 && (*hlGray.buffer)[4] == 180 &&
                  (*hlGray.buffer)[5] == 180,
              "highlightMap similar pixel equals Grayscale8 base");
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

    // Vectorized Grayscale8 diffMap + contiguous amplify & computeStats
    {
        // 48x4 image to exercise AVX2 (32B), SSE2 (16B), and scalar tails
        const int w = 48;
        const int h = 4;
        auto bufA = std::make_shared<std::vector<uint8_t>>(w * h, uint8_t(50));
        auto bufB = std::make_shared<std::vector<uint8_t>>(w * h, uint8_t(50));
        (*bufA)[0] = 100; // diff 50
        (*bufA)[33] = 70; // diff 20 in SSE2 span
        (*bufA)[47] = 80; // diff 30 in tail
        ImageData a;
        a.buffer = bufA;
        a.width = w;
        a.height = h;
        a.format = PixelFormat::Grayscale8;
        ImageData b;
        b.buffer = bufB;
        b.width = w;
        b.height = h;
        b.format = PixelFormat::Grayscale8;

        auto diff = DifferenceEngine::differenceMap(a, b);
        CHECK(!diff.isNull(), "vectorized Grayscale8 diff non-null");
        CHECK((*diff.buffer)[0] == 50, "diff at 0 is 50");
        CHECK((*diff.buffer)[33] == 20, "diff at 33 is 20");
        CHECK((*diff.buffer)[47] == 30, "diff at 47 is 30");

        auto amp = DifferenceEngine::amplify(diff, 2.0);
        CHECK(!amp.isNull(), "contiguous amplify non-null");
        CHECK((*amp.buffer)[0] == 100, "amplified diff at 0 is 100");
        CHECK((*amp.buffer)[33] == 40, "amplified diff at 33 is 40");

        const auto stats = DifferenceEngine::computeStats(diff);
        CHECK(stats.totalPixels == w * h, "contiguous computeStats total matches");
        CHECK(stats.diffPixels == 3, "contiguous computeStats diff count matches");
        CHECK(stats.maxDiff == 50, "contiguous computeStats maxDiff is 50");
    }

    // Cross-format RGB24 vs BGR24
    {
        const int w = 16, h = 16;
        auto rgbBuf = std::make_shared<std::vector<uint8_t>>(w * h * 3);
        auto bgrBuf = std::make_shared<std::vector<uint8_t>>(w * h * 3);
        for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i)
        {
            (*rgbBuf)[i * 3 + 0] = 200; // R
            (*rgbBuf)[i * 3 + 1] = 100; // G
            (*rgbBuf)[i * 3 + 2] = 50;  // B

            (*bgrBuf)[i * 3 + 0] = 50;  // B
            (*bgrBuf)[i * 3 + 1] = 100; // G
            (*bgrBuf)[i * 3 + 2] = 200; // R
        }
        ImageData rgbImg;
        rgbImg.buffer = rgbBuf;
        rgbImg.width = w;
        rgbImg.height = h;
        rgbImg.format = PixelFormat::RGB24;

        ImageData bgrImg;
        bgrImg.buffer = bgrBuf;
        bgrImg.width = w;
        bgrImg.height = h;
        bgrImg.format = PixelFormat::BGR24;

        auto diff = DifferenceEngine::differenceMap(rgbImg, bgrImg);
        CHECK(!diff.isNull(), "cross-format RGB24 vs BGR24 diff non-null");
        CHECK(isAllBlack(diff), "cross-format identical colors produce zero diff");

        // Perturb one pixel in bgrImg
        (*bgrBuf)[0] = 255;
        auto diffPerturbed = DifferenceEngine::differenceMap(rgbImg, bgrImg);
        CHECK(!diffPerturbed.isNull(), "perturbed cross-format non-null");
        CHECK((*diffPerturbed.buffer)[0] > 0, "perturbed pixel shows diff");
        CHECK((*diffPerturbed.buffer)[1] == 0, "adjacent pixel remains zero diff");
    }

    // Cross-format RGBA32 vs BGRA32
    {
        const int w = 8, h = 8;
        auto rgbaBuf = std::make_shared<std::vector<uint8_t>>(w * h * 4);
        auto bgraBuf = std::make_shared<std::vector<uint8_t>>(w * h * 4);
        for (size_t i = 0; i < static_cast<size_t>(w) * static_cast<size_t>(h); ++i)
        {
            (*rgbaBuf)[i * 4 + 0] = 180; // R
            (*rgbaBuf)[i * 4 + 1] = 90;  // G
            (*rgbaBuf)[i * 4 + 2] = 40;  // B
            (*rgbaBuf)[i * 4 + 3] = 255; // A

            (*bgraBuf)[i * 4 + 0] = 40;  // B
            (*bgraBuf)[i * 4 + 1] = 90;  // G
            (*bgraBuf)[i * 4 + 2] = 180; // R
            (*bgraBuf)[i * 4 + 3] = 255; // A
        }
        ImageData rgbaImg;
        rgbaImg.buffer = rgbaBuf;
        rgbaImg.width = w;
        rgbaImg.height = h;
        rgbaImg.format = PixelFormat::RGBA32;

        ImageData bgraImg;
        bgraImg.buffer = bgraBuf;
        bgraImg.width = w;
        bgraImg.height = h;
        bgraImg.format = PixelFormat::BGRA32;

        auto diff = DifferenceEngine::differenceMap(rgbaImg, bgraImg);
        CHECK(!diff.isNull(), "cross-format RGBA32 vs BGRA32 diff non-null");
        CHECK(isAllBlack(diff), "cross-format RGBA vs BGRA identical colors produce zero diff");
    }

    // 64-bit coordinate clamping & integer overflow protection in computeStats
    {
        auto a = makeSolidRgb(10, 10, 100, 100, 100);
        auto b = makeSolidRgb(10, 10, 100, 100, 100);
        auto diff = DifferenceEngine::differenceMap(a, b);

        // Extreme positive offset that would overflow 32-bit int if added
        const auto ovf =
            DifferenceEngine::computeStats(diff, 0, 2000000000, 2000000000, 2000000000, 2000000000);
        CHECK(ovf.totalPixels == 0, "extreme ROI offset safely clamps without overflow");

        // Negative coordinates that fully lie outside
        const auto negOut = DifferenceEngine::computeStats(diff, 0, -100, -100, 50, 50);
        CHECK(negOut.totalPixels == 0, "negative fully outside ROI yields 0 pixels");

        // Negative start coordinate with overlap
        const auto negOverlap = DifferenceEngine::computeStats(diff, 0, -2, -2, 5, 5);
        CHECK(negOverlap.totalPixels == 9, "negative overlapping ROI clamps to [0,3)x[0,3)");
    }

    // HighlightMap with BGR24 base image
    {
        const int w = 4, h = 4;
        auto diffBuf = std::make_shared<std::vector<uint8_t>>(w * h, uint8_t(0));
        (*diffBuf)[0] = 50; // diff at (0,0)
        ImageData diffImg;
        diffImg.buffer = diffBuf;
        diffImg.width = w;
        diffImg.height = h;
        diffImg.format = PixelFormat::Grayscale8;

        auto bgrBuf = std::make_shared<std::vector<uint8_t>>(w * h * 3, uint8_t(100));
        ImageData bgrImg;
        bgrImg.buffer = bgrBuf;
        bgrImg.width = w;
        bgrImg.height = h;
        bgrImg.format = PixelFormat::BGR24;

        auto hl = DifferenceEngine::highlightMap(diffImg, bgrImg, 10);
        CHECK(!hl.isNull(), "highlightMap with BGR24 base non-null");
        CHECK((*hl.buffer)[0] > (*hl.buffer)[1] && (*hl.buffer)[0] > (*hl.buffer)[2],
              "highlightMap BGR diff pixel is red");
        // Similar pixel (offset 3) should have equal R, G, B channels
        CHECK((*hl.buffer)[3] == (*hl.buffer)[4] && (*hl.buffer)[4] == (*hl.buffer)[5],
              "highlightMap BGR similar pixel is gray");
    }

    std::cout << "\nDifferenceEngine: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
