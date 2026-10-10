#include "core/render/DisplayResample.h"

#include <QCoreApplication>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <thread>
#include <vector>

namespace
{

using mviewer::core::DisplayResampleRequest;

int g_failures = 0;

void expect(bool ok, const char *name)
{
    std::printf("%s: %s\n", ok ? "PASS" : "FAIL", name);
    if (!ok)
        ++g_failures;
}

ImageData makePattern(int w, int h)
{
    ImageData image = makeImageData(w, h, PixelFormat::RGB24);
    uint8_t *dst = image.buffer->data();
    for (int y = 0; y < h; ++y)
    {
        uint8_t *row = dst + static_cast<size_t>(y) * static_cast<size_t>(w) * 3u;
        for (int x = 0; x < w; ++x)
        {
            row[static_cast<size_t>(x) * 3u] = static_cast<uint8_t>((x * 3 + y) & 255);
            row[static_cast<size_t>(x) * 3u + 1] = static_cast<uint8_t>((x + y * 5) & 255);
            row[static_cast<size_t>(x) * 3u + 2] = static_cast<uint8_t>((x * 7 + y * 2) & 255);
        }
    }
    return image;
}

DisplayResampleRequest fullFrame(const ImageData &image, int targetW, int targetH)
{
    DisplayResampleRequest request;
    request.source = {0, 0, image.width, image.height};
    request.targetWidth = targetW;
    request.targetHeight = targetH;
    return request;
}

double milliseconds(std::chrono::steady_clock::time_point start)
{
    const auto elapsed = std::chrono::steady_clock::now() - start;
    return std::chrono::duration<double, std::milli>(elapsed).count();
}

double timeOnce(const ImageData &image, int targetW, int targetH)
{
    const DisplayResampleRequest request = fullFrame(image, targetW, targetH);
    const auto start = std::chrono::steady_clock::now();
    const ImageData out = mviewer::core::resampleDisplay(image, request);
    const double ms = milliseconds(start);
    const bool ok = !out.isNull() && out.width == targetW && out.height == targetH &&
                    out.format == PixelFormat::RGB24;
    if (!ok)
        ++g_failures;
    return ms;
}

void reportCase(const char *name, double ms, double budgetMs)
{
    std::printf("TIME %s: %.2f ms (budget %.0f ms)\n", name, ms, budgetMs);
    expect(ms < budgetMs, name);
}

void benchSingle(const ImageData &image, int targetW, int targetH, const char *name,
                 double budgetMs)
{
    const double cold = timeOnce(image, targetW, targetH);
    const double warm = timeOnce(image, targetW, targetH);
    std::printf("  cold %.2f ms, warm %.2f ms\n", cold, warm);
    reportCase(name, cold, budgetMs);
}

void benchEight(const ImageData &image, int targetW, int targetH, const char *name, double budgetMs)
{
    constexpr int kPanes = 8;
    std::vector<ImageData> sources;
    sources.reserve(kPanes);
    for (int i = 0; i < kPanes; ++i)
        sources.push_back(image);
    std::atomic<int> bad{0};
    const auto start = std::chrono::steady_clock::now();
    std::vector<std::thread> threads;
    threads.reserve(kPanes);
    for (int i = 0; i < kPanes; ++i)
    {
        threads.emplace_back(
            [&, i]()
            {
                const ImageData out = mviewer::core::resampleDisplay(
                    sources[static_cast<size_t>(i)], fullFrame(image, targetW, targetH));
                if (out.isNull() || out.width != targetW || out.height != targetH)
                    bad.fetch_add(1);
            });
    }
    for (std::thread &thread : threads)
        thread.join();
    const double ms = milliseconds(start);
    std::printf("  wall %.2f ms\n", ms);
    expect(bad.load() == 0, "8 panes produced full rasters");
    reportCase(name, ms, budgetMs);
}

} // namespace

int main(int argc, char **argv)
{
    QCoreApplication app(argc, argv);
    (void)app;
    std::printf("display resample benchmark\n");
    const ImageData large = makePattern(6000, 4000);
    const ImageData medium = makePattern(1600, 1100);
    // Warm the instruction cache and any one-time dispatch before the cold sample
    // of the small case only. Large-case cold still includes first-touch of that
    // buffer. Budgets are loose ceilings so a busy CI host does not flake.
    (void)timeOnce(medium, 1200, 825);

    benchSingle(large, 1400, 933, "6000x4000 -> 1400x933", 8000.0);
    benchSingle(large, 700, 466, "6000x4000 -> 700x466", 8000.0);
    benchSingle(medium, 1200, 825, "1600x1100 -> 1200x825", 4000.0);
    benchEight(large, 1400, 933, "8 panes 6000x4000 -> 1400x933", 20000.0);

    std::printf("=== Display resample bench: %s ===\n", g_failures == 0 ? "PASS" : "FAIL");
    return g_failures == 0 ? 0 : 1;
}
