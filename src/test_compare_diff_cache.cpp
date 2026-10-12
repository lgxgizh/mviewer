// Threshold drags reuse cached adjusted images, alignment, diff map, PSNR and
// SSIM. A warm (cached) batch must equal a cold one bit for bit, for every
// threshold and after adjustment changes.
// Optional bench: MVIEWER_BENCH_DIFF=1 compares two 24 MP frames.
// NOLINTBEGIN
#include "compareworkspace.h"
#include "compareworkspace_types.h"

#include <QApplication>
#include <QElapsedTimer>

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <random>

static int g_pass = 0;
static int g_fail = 0;
#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (c)                                                                                     \
            ++g_pass;                                                                              \
        else                                                                                       \
        {                                                                                          \
            ++g_fail;                                                                              \
            printf("FAIL: %s\n", m);                                                               \
        }                                                                                          \
    } while (0)

struct CompareDiffCacheTestAccess
{
    static mviewer::cw::DiffBatchResult run(const std::vector<ImageData> &pixels, uint8_t threshold,
                                            bool align, int brightness)
    {
        std::vector<CompareWorkspace::CellAdjust> adjusts(pixels.size());
        adjusts[1].brightness = brightness;
        const std::vector<QSize> targets(pixels.size(), QSize());
        TaskScheduler::TaskContext context;
        return CompareWorkspace::computeDiffBatch(pixels, targets, adjusts, 0, threshold, 1.0, true,
                                                  true, align, mviewer::domain::Selection{}, 2, 1,
                                                  context);
    }
};

static ImageData makeFrame(int w, int h, unsigned seed, int shift)
{
    ImageData d = makeImageData(w, h, PixelFormat::RGB24);
    std::mt19937 rng(seed);
    for (int y = 0; y < h; ++y)
        for (int x = 0; x < w; ++x)
        {
            uint8_t *p = d.buffer->data() + (static_cast<size_t>(y) * w + x) * 3;
            const int v = ((x + shift) * 3 + y * 2) & 255;
            p[0] = static_cast<uint8_t>(v);
            p[1] = static_cast<uint8_t>((v + (rng() & 15)) & 255);
            p[2] = static_cast<uint8_t>(255 - v);
        }
    return d;
}

static bool sameStats(const DifferenceEngine::DiffStats &a, const DifferenceEngine::DiffStats &b)
{
    return a.totalPixels == b.totalPixels && a.diffPixels == b.diffPixels &&
           a.diffRatio == b.diffRatio && a.meanDiff == b.meanDiff && a.maxDiff == b.maxDiff &&
           a.stdDevDiff == b.stdDevDiff;
}

static bool sameResult(const mviewer::cw::DiffBatchResult &a, const mviewer::cw::DiffBatchResult &b)
{
    if (a.targetIdx != b.targetIdx || a.aligned != b.aligned || a.alignX != b.alignX ||
        a.alignY != b.alignY || a.metricsValid != b.metricsValid || a.psnr != b.psnr ||
        a.ssim != b.ssim || a.hasStats != b.hasStats || !sameStats(a.stats, b.stats) ||
        a.suggestedThreshold != b.suggestedThreshold || a.overlays.size() != b.overlays.size())
        return false;
    for (size_t i = 0; i < a.overlays.size(); ++i)
    {
        const auto &x = a.overlays[i];
        const auto &y = b.overlays[i];
        if (x.index != y.index || x.sizeMismatch != y.sizeMismatch || x.overlay != y.overlay ||
            x.opacity != y.opacity || x.psnr != y.psnr || x.ssim != y.ssim ||
            x.hasMetrics != y.hasMetrics)
            return false;
    }
    return true;
}

static void testWarmEqualsCold(int w, int h, bool printTimes)
{
    printf("\n[diff cache %dx%d]\n", w, h);
    const std::vector<ImageData> pixels = {makeFrame(w, h, 1, 0), makeFrame(w, h, 2, 3)};
    for (const bool align : {false, true})
    {
        CompareWorkspace::clearDiffCache();
        QElapsedTimer t;
        t.start();
        (void)CompareDiffCacheTestAccess::run(pixels, 20, align, 0); // warms the cache
        const qint64 coldMs = t.elapsed();
        qint64 warmTotal = 0;
        int warmRuns = 0;
        bool allEqual = true;
        for (const int threshold : {5, 20, 64, 128, 250})
        {
            t.restart();
            const auto warm = CompareDiffCacheTestAccess::run(pixels, threshold, align, 0);
            warmTotal += t.elapsed();
            ++warmRuns;
            CompareWorkspace::clearDiffCache();
            const auto cold = CompareDiffCacheTestAccess::run(pixels, threshold, align, 0);
            allEqual = allEqual && sameResult(warm, cold);
        }
        CHECK(allEqual, align ? "aligned: warm threshold results equal cold results"
                              : "warm threshold results equal cold results");
        // An adjustment change must not reuse the old diff.
        (void)CompareDiffCacheTestAccess::run(pixels, 20, align, 0);
        const auto warmAdjusted = CompareDiffCacheTestAccess::run(pixels, 20, align, 12);
        CompareWorkspace::clearDiffCache();
        const auto coldAdjusted = CompareDiffCacheTestAccess::run(pixels, 20, align, 12);
        CHECK(sameResult(warmAdjusted, coldAdjusted), "adjustment change recomputes the pair");
        if (printTimes)
            printf("  align=%d: full batch %lld ms, threshold-only batch %.1f ms\n", align ? 1 : 0,
                   coldMs, static_cast<double>(warmTotal) / warmRuns);
    }
    CompareWorkspace::clearDiffCache();
}

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    testWarmEqualsCold(640, 480, true);
    if (std::getenv("MVIEWER_BENCH_DIFF"))
        testWarmEqualsCold(6000, 4000, true);
    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail == 0 ? 0 : 1;
}
// NOLINTEND
