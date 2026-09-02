// M61 deterministic recorder: latency is reported, behavioral bounds are asserted.

#include "core/image/ImageStats.h"

#include <chrono>
#include <cstdint>
#include <cstdio>

namespace
{
int g_failures = 0;

void record(const ImageData &image, const mviewer::domain::Selection &roi, const char *name)
{
    const auto start = std::chrono::steady_clock::now();
    const auto stats = mviewer::core::computeROIChannelStats(image, roi);
    const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                             std::chrono::steady_clock::now() - start)
                             .count();
    const int64_t expected = static_cast<int64_t>(roi.width) * roi.height;
    std::printf("M61_ROI_BENCH name=%s pixels=%lld elapsed_ms=%lld\n", name,
                static_cast<long long>(expected), static_cast<long long>(elapsed));
    if (!stats.valid || stats.pixelCount != expected)
    {
        std::printf("FAIL: %s pixel accounting\n", name);
        ++g_failures;
    }
}
} // namespace

int main()
{
    // One 100 MP 8-bit buffer bounds peak fixture memory while three nested
    // regions record 24/60/100 MP traversal. No machine-specific time is a
    // gate; exact count and cancellation checkpoint behavior are deterministic.
    const ImageData image = makeImageData(10000, 10000, PixelFormat::Grayscale8);
    record(image, {0, 0, 6000, 4000}, "24MP");
    record(image, {0, 0, 10000, 6000}, "60MP");
    record(image, {0, 0, 10000, 10000}, "100MP");

    int checkpoints = 0;
    const auto cancelStart = std::chrono::steady_clock::now();
    const auto cancelled = mviewer::core::computeROIChannelStats(image, {0, 0, 10000, 10000}, [&]()
                                                                 { return ++checkpoints >= 9; });
    const auto cancelElapsed = std::chrono::duration_cast<std::chrono::microseconds>(
                                   std::chrono::steady_clock::now() - cancelStart)
                                   .count();
    std::printf("M61_ROI_CANCEL checkpoints=%d exit_us=%lld\n", checkpoints,
                static_cast<long long>(cancelElapsed));
    if (!cancelled.cancelled || cancelled.valid || checkpoints != 9)
    {
        std::printf("FAIL: cancellation did not exit at the bounded row checkpoint\n");
        ++g_failures;
    }
    return g_failures == 0 ? 0 : 1;
}
