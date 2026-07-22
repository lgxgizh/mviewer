// DifferenceEngine unit tests — pixel diff / heatmap / threshold.
#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageBuffer.h"
#include <QApplication>
#include <iostream>
#include <vector>

static int g_fail = 0;
#define CHECK(cond, msg) \
    do { \
        if (!(cond)) { std::cerr << "FAIL: " << msg << "\n"; ++g_fail; } \
        else { std::cout << "PASS: " << msg << "\n"; } \
    } while (0)

static ImageData makeSolidRgb(int w, int h, uint8_t r, uint8_t g, uint8_t b)
{
    const size_t sz = static_cast<size_t>(w) * h * 3;
    auto buf = std::make_shared<std::vector<uint8_t>>(sz);
    for (size_t i = 0; i < sz; i += 3) {
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
    if (img.isNull()) return false;
    for (auto v : *img.buffer) if (v != 0) return false;
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
        for (auto v : *diffRaw.buffer) if (v > 0) ++rawNZ;
        for (auto v : *diffThresh.buffer) if (v > 0) ++threshNZ;
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

    std::cout << "\nDifferenceEngine: " << (g_fail == 0 ? "ALL PASSED" : "FAILURES") << "\n";
    return g_fail == 0 ? 0 : 1;
}
