#include "previewpanel.h"
#include "thumbnailpanel.h"

#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <QApplication>
#include <QBuffer>
#include <QDir>
#include <QElapsedTimer>
#include <QEventLoop>
#include <QFile>
#include <QImage>
#include <QImageWriter>
#include <QTemporaryDir>

#include <array>
#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <mutex>
#include <thread>
#include <vector>

namespace
{

int g_failures = 0;

struct Metrics
{
    qint64 directoryShellMs = -1;
    qint64 firstGalleryRowMs = -1;
    qint64 firstThumbnailMs = -1;
    qint64 firstScreen50Ms = -1;
    qint64 firstScreen90Ms = -1;
    qint64 firstSelectedPreviewMs = -1;
    qint64 selectedPreviewP50Ms = -1;
    qint64 selectedPreviewP95Ms = -1;
    qint64 directoryScanCompleteMs = -1;
    qint64 galleryStableMs = -1;
};

bool waitUntil(const std::function<bool()> &predicate, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 2);
        if (predicate())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    QApplication::processEvents(QEventLoop::AllEvents, 2);
    return predicate();
}

void writeSeed(const QString &path)
{
    QImage image(2, 2, QImage::Format_RGB32);
    image.fill(qRgb(37, 89, 143));
    if (!image.save(path, "PNG"))
        std::printf("failed to write benchmark seed: %s\n", path.toUtf8().constData());
}

bool createImages(const QString &directory, int count, const QString &seed,
                  const QString &prefix = QStringLiteral("img_"))
{
    if (!QDir().mkpath(directory))
        return false;
    for (int i = 0; i < count; ++i)
    {
        const QString name = QStringLiteral("%1%2.png").arg(prefix).arg(i, 6, 10, QLatin1Char('0'));
        if (!QFile::copy(seed, QDir(directory).filePath(name)))
            return false;
    }
    return true;
}

int firstScreenCount(const ThumbnailPanel &panel)
{
    const int cellW = std::max(1, panel.gridSize().width());
    const int cellH = std::max(1, panel.gridSize().height());
    const int cols = std::max(1, panel.viewport()->width() / cellW);
    const int rows = std::max(1, panel.viewport()->height() / cellH + 1);
    return cols * rows;
}

int readyCount(const ThumbnailPanel &panel, const QStringList &paths)
{
    int ready = 0;
    for (const QString &path : paths)
        if (!panel.thumbReady(path).isNull())
            ++ready;
    return ready;
}

Metrics measureDirectory(const QString &directory, int expected)
{
    Metrics metrics;
    ThumbnailPipeline::instance().clear();

    ThumbnailPanel panel;
    panel.resize(900, 620);
    panel.show();
    QApplication::processEvents(QEventLoop::AllEvents, 5);

    QElapsedTimer timer;
    timer.start();
    bool stable = false;
    QObject::connect(&panel, &ThumbnailPanel::sequenceChanged, &panel,
                     [&](const QString &path, const QStringList &paths)
                     {
                         if (path == directory && paths.size() == expected &&
                             panel.entries().size() == expected)
                         {
                             if (metrics.directoryScanCompleteMs < 0)
                                 metrics.directoryScanCompleteMs = timer.elapsed();
                             stable = true;
                             if (metrics.galleryStableMs < 0)
                                 metrics.galleryStableMs = timer.elapsed();
                         }
                     });

    panel.setDirectory(directory);
    metrics.directoryShellMs = timer.elapsed();

    const auto recordScanComplete = [&]
    {
        if (metrics.directoryScanCompleteMs < 0 && panel.entries().size() == expected)
            metrics.directoryScanCompleteMs = timer.elapsed();
    };

    waitUntil([&] { return !panel.pathList().isEmpty(); }, 60000);
    metrics.firstGalleryRowMs = timer.elapsed();
    recordScanComplete();

    const QStringList screenPaths = panel.pathList().first(firstScreenCount(panel));
    waitUntil(
        [&]
        {
            recordScanComplete();
            return readyCount(panel, screenPaths) >= 1;
        },
        60000);
    metrics.firstThumbnailMs = timer.elapsed();

    const int screenTotal = std::max(1, static_cast<int>(screenPaths.size()));
    waitUntil(
        [&]
        {
            recordScanComplete();
            return readyCount(panel, screenPaths) * 2 >= screenTotal;
        },
        60000);
    metrics.firstScreen50Ms = timer.elapsed();
    waitUntil(
        [&]
        {
            recordScanComplete();
            return readyCount(panel, screenPaths) * 10 >= screenTotal * 9;
        },
        60000);
    metrics.firstScreen90Ms = timer.elapsed();

    if (!screenPaths.isEmpty())
    {
        PreviewPanel preview;
        preview.resize(320, 260);
        preview.show();
        QApplication::processEvents(QEventLoop::AllEvents, 2);
        std::vector<qint64> previewSamples;
        const int sampleCount = std::min(20, static_cast<int>(screenPaths.size()));
        previewSamples.reserve(sampleCount);
        for (int i = 0; i < sampleCount; ++i)
        {
            preview.setImage(QString());
            QApplication::processEvents(QEventLoop::AllEvents, 2);
            QElapsedTimer previewTimer;
            previewTimer.start();
            // Exercise the real selected-preview path. The gallery thumbnail
            // is intentionally not supplied here so this records the first
            // async preview frame rather than a synchronous warm-thumbnail
            // handoff.
            preview.setImage(screenPaths[i]);
            waitUntil([&] { return preview.hasImage(); }, 60000);
            if (preview.hasImage())
            {
                const qint64 elapsed = previewTimer.elapsed();
                previewSamples.push_back(elapsed);
                if (i == 0)
                    metrics.firstSelectedPreviewMs = elapsed;
            }
            waitUntil(
                [&]
                {
                    recordScanComplete();
                    return preview.presentationQuality() ==
                           PreviewPanel::PresentationQuality::Preview;
                },
                60000);
        }
        if (!previewSamples.empty())
        {
            std::sort(previewSamples.begin(), previewSamples.end());
            metrics.selectedPreviewP50Ms = previewSamples[previewSamples.size() / 2];
            const size_t p95Index = (previewSamples.size() * 95 + 99) / 100 - 1;
            metrics.selectedPreviewP95Ms = previewSamples[p95Index];
        }
        ThumbnailPipeline::instance().clear();
    }

    waitUntil(
        [&]
        {
            recordScanComplete();
            return metrics.directoryScanCompleteMs >= 0;
        },
        60000);
    waitUntil([&] { return stable; }, 60000);

    waitUntil(
        []
        {
            return ThumbnailPipeline::instance().pendingCount() == 0 &&
                   ThumbnailPipeline::instance().handlesCount() == 0;
        },
        60000);
    TaskScheduler::instance().drain(TaskScheduler::PoolType::DecodePool,
                                    std::chrono::seconds(60));
    return metrics;
}

void printMetrics(const char *label, const Metrics &m)
{
    std::printf(
        "%s shell=%lld first_row=%lld first_thumb=%lld screen50=%lld screen90=%lld "
        "selected_preview=%lld selected_preview_p50=%lld selected_preview_p95=%lld "
        "scan_complete=%lld stable=%lld\n",
        label, static_cast<long long>(m.directoryShellMs),
        static_cast<long long>(m.firstGalleryRowMs), static_cast<long long>(m.firstThumbnailMs),
        static_cast<long long>(m.firstScreen50Ms), static_cast<long long>(m.firstScreen90Ms),
        static_cast<long long>(m.firstSelectedPreviewMs),
        static_cast<long long>(m.selectedPreviewP50Ms),
        static_cast<long long>(m.selectedPreviewP95Ms),
        static_cast<long long>(m.directoryScanCompleteMs),
        static_cast<long long>(m.galleryStableMs));
}

void checkMetrics(const char *label, const Metrics &metrics, int expected)
{
    const bool allRecorded = metrics.firstGalleryRowMs >= 0 && metrics.firstThumbnailMs >= 0 &&
                             metrics.firstScreen50Ms >= 0 && metrics.firstScreen90Ms >= 0 &&
                             metrics.firstSelectedPreviewMs >= 0 &&
                             metrics.selectedPreviewP50Ms >= 0 && metrics.selectedPreviewP95Ms >= 0 &&
                             metrics.directoryScanCompleteMs >= 0 && metrics.galleryStableMs >= 0;
    if (!allRecorded)
    {
        std::printf("FAIL %s: one or more Browse E2E milestones were not recorded\n", label);
        ++g_failures;
    }
    if (expected >= 1000 && metrics.directoryScanCompleteMs < metrics.firstGalleryRowMs)
    {
        std::printf("FAIL %s: scan completion precedes first gallery row\n", label);
        ++g_failures;
    }
    if (expected >= 1000 && metrics.firstGalleryRowMs >= metrics.directoryScanCompleteMs)
    {
        std::printf("FAIL %s: large source did not publish rows before scan completion\n", label);
        ++g_failures;
    }
    if (metrics.galleryStableMs < metrics.directoryScanCompleteMs)
    {
        std::printf("FAIL %s: gallery stabilized before scan completion\n", label);
        ++g_failures;
    }
}

void testPngEncodingBenchmark()
{
    QImage image(512, 512, QImage::Format_RGB32);
    for (int y = 0; y < image.height(); ++y)
    {
        auto *scanline = reinterpret_cast<QRgb *>(image.scanLine(y));
        for (int x = 0; x < image.width(); ++x)
            scanline[x] = qRgb((x * 17 + y * 3) % 256, (x * 5 + y * 11) % 256,
                               (x * 13 + y * 7) % 256);
    }

    const auto measure = [&](int compression)
    {
        std::vector<qint64> samples;
        samples.reserve(20);
        qint64 bytes = 0;
        for (int i = 0; i < 22; ++i)
        {
            QBuffer buffer;
            buffer.open(QIODevice::WriteOnly);
            QImageWriter writer(&buffer, "png");
            writer.setCompression(compression);
            QElapsedTimer timer;
            timer.start();
            if (!writer.write(image))
            {
                std::printf("FAIL png encoding benchmark: %s\n",
                            writer.errorString().toUtf8().constData());
                ++g_failures;
                return std::pair<qint64, qint64>{-1, -1};
            }
            if (i >= 2)
                samples.push_back(timer.elapsed());
            bytes = buffer.size();
        }
        std::sort(samples.begin(), samples.end());
        return std::pair<qint64, qint64>{samples[samples.size() / 2], bytes};
    };

    const auto defaultEncoding = measure(-1);
    const auto fastEncoding = measure(1);
    if (defaultEncoding.first >= 0 && fastEncoding.first >= 0)
    {
        std::printf("png_default_encode_p50=%lld png_fast_encode_p50=%lld "
                    "png_default_bytes=%lld png_fast_bytes=%lld\n",
                    static_cast<long long>(defaultEncoding.first),
                    static_cast<long long>(fastEncoding.first),
                    static_cast<long long>(defaultEncoding.second),
                    static_cast<long long>(fastEncoding.second));
    }
}

void testViewportLatestWins()
{
    ThumbnailPipeline pipeline;
    std::atomic<int> decodeCalls{0};
    std::atomic<int> delivered{0};
    pipeline.setDecodeFn(
        [&](const std::string &, int)
        {
            decodeCalls.fetch_add(1);
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
            return ImageData{};
        });
    std::mutex jumpMutex;
    std::string expectedTarget;
    std::chrono::steady_clock::time_point jumpStarted;
    std::vector<qint64> jumpLatencyMs;
    pipeline.setResultFn(
        [&](const std::string &path, int, const ImageData &)
        {
            std::lock_guard<std::mutex> lock(jumpMutex);
            ++delivered;
            if (!expectedTarget.empty() && path == expectedTarget)
            {
                jumpLatencyMs.push_back(std::chrono::duration_cast<std::chrono::milliseconds>(
                                            std::chrono::steady_clock::now() - jumpStarted)
                                            .count());
                expectedTarget.clear();
            }
        });
    std::vector<std::string> sources;
    sources.reserve(10000);
    for (int i = 0; i < 10000; ++i)
        sources.push_back("m54/viewport/" + std::to_string(i) + ".png");
    pipeline.setSources(sources);
    pipeline.setPredictiveCount(96);
    size_t peakPending = 0;
    for (size_t begin = 0; begin < 4000; begin += 80)
    {
        pipeline.setVisibleRange(begin, begin + 16);
        peakPending = std::max(peakPending, pipeline.pendingCount());
    }
    // Start the jump series on a fresh generation. This prevents an item that
    // was also present in the burst's tail (for example index 500) from
    // sharing same-generation bookkeeping with a new visible request.
    pipeline.clear();
    TaskScheduler::instance().drain(TaskScheduler::PoolType::ThumbnailPool,
                                    std::chrono::seconds(30));
    pipeline.setSources(sources);
    // Measure jump-to-current latency without adding another predictive tail
    // to each jump. The rapid-scroll burst above already proves that the
    // predictive window stays bounded; this phase isolates latest-wins
    // delivery of the requested visible item.
    pipeline.setPredictiveCount(0);
    const std::array<size_t, 10> jumps = {500, 5000, 9000, 1200, 4800,
                                          8800, 240, 7600, 3200, 960};
    for (const size_t begin : jumps)
    {
        {
            std::lock_guard<std::mutex> lock(jumpMutex);
            expectedTarget = sources[begin];
            jumpStarted = std::chrono::steady_clock::now();
        }
        pipeline.setVisibleRange(begin, begin + 16);
        peakPending = std::max(peakPending, pipeline.pendingCount());
        const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(15);
        for (;;)
        {
            {
                std::lock_guard<std::mutex> lock(jumpMutex);
                if (expectedTarget.empty())
                    break;
            }
            if (std::chrono::steady_clock::now() >= deadline)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        {
            std::lock_guard<std::mutex> lock(jumpMutex);
            if (!expectedTarget.empty())
                std::printf("viewport_missing_target=%zu\n", begin);
        }
    }
    if (peakPending > 112)
    {
        std::printf("FAIL viewport latest-wins: peak pending=%zu\n", peakPending);
        ++g_failures;
    }
    if (jumpLatencyMs.size() != jumps.size())
    {
        std::printf("FAIL viewport latest-wins: only %zu/%zu current targets delivered\n",
                    jumpLatencyMs.size(), jumps.size());
        ++g_failures;
    }
    else
    {
        std::vector<qint64> sorted = jumpLatencyMs;
        std::sort(sorted.begin(), sorted.end());
        const size_t p95Index = (sorted.size() * 95 + 99) / 100 - 1;
        std::printf("scroll_jump_p95=%lld peak_thumbnail_queue=%zu wasted_decode_work=%d\n",
                    static_cast<long long>(sorted[p95Index]), peakPending,
                    decodeCalls.load() - delivered.load());
    }
    pipeline.clear();
    TaskScheduler::instance().drain(TaskScheduler::PoolType::ThumbnailPool,
                                    std::chrono::seconds(30));
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir fixture;
    if (!fixture.isValid())
        return 2;

    const QString seed = QDir(fixture.path()).filePath(QStringLiteral("seed.png"));
    writeSeed(seed);
    const QString root = fixture.path();
    const QString dir100 = QDir(root).filePath(QStringLiteral("images_100"));
    const QString dir1000 = QDir(root).filePath(QStringLiteral("images_1000"));
    const QString dir10000 = QDir(root).filePath(QStringLiteral("images_10000"));
    const QString dir50000 = QDir(root).filePath(QStringLiteral("entries_50000"));
    if (!createImages(dir100, 100, seed) || !createImages(dir1000, 1000, seed) ||
        !createImages(dir10000, 10000, seed) || !createImages(dir50000, 50000, seed))
        return 2;

    std::printf("M54 perceived browse benchmark (ms; cold then warm disk/cache)\n");
    for (const auto &caseInfo : {
             std::pair<const char *, std::pair<QString, int>>{"100", {dir100, 100}},
             std::pair<const char *, std::pair<QString, int>>{"1000", {dir1000, 1000}},
             std::pair<const char *, std::pair<QString, int>>{"10000", {dir10000, 10000}},
             std::pair<const char *, std::pair<QString, int>>{"50000 entries", {dir50000, 50000}}})
    {
        const Metrics cold = measureDirectory(caseInfo.second.first, caseInfo.second.second);
        const std::string coldLabel = std::string(caseInfo.first) + " cold";
        printMetrics(coldLabel.c_str(), cold);
        checkMetrics(coldLabel.c_str(), cold, caseInfo.second.second);
        const Metrics warm = measureDirectory(caseInfo.second.first, caseInfo.second.second);
        const std::string warmLabel = std::string(caseInfo.first) + " warm";
        printMetrics(warmLabel.c_str(), warm);
        checkMetrics(warmLabel.c_str(), warm, caseInfo.second.second);
    }

    testPngEncodingBenchmark();
    testViewportLatestWins();
    std::printf("M54 benchmark complete\n");
    return g_failures == 0 ? 0 : 1;
}
