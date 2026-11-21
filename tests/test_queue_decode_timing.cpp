// Queue wait (worker start minus TaskContext::enqueuedAt) versus the worker's
// own open + raster decode. Generated temp JPEGs only. Does not change
// scheduler width except for the forced-wait case, which restores it.

#include "core/scheduler/TaskScheduler.h"
#include "imageviewer.h"
#include "imageviewer_queuedecode.h"

#include <QApplication>
#include <QColor>
#include <QEventLoop>
#include <QImage>
#include <QTemporaryDir>
#include <QThread>

#include <algorithm>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstdio>
#include <functional>
#include <optional>
#include <string>
#include <thread>

static int g_failures = 0;

#define CHECK(c, m)                                                                                \
    do                                                                                             \
    {                                                                                              \
        if (!(c))                                                                                  \
        {                                                                                          \
            std::printf("FAIL: %s\n", m);                                                          \
            std::fflush(stdout);                                                                   \
            ++g_failures;                                                                          \
        }                                                                                          \
    } while (false)

namespace
{

constexpr int kFastW = 2560;
constexpr int kFastH = 1800;
constexpr int kSlowW = 4096;
constexpr int kSlowH = 3072;
constexpr int kReadyMs = 30000;

void pump(int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
        QApplication::processEvents(QEventLoop::AllEvents, 1);
}

bool waitTrue(const std::function<bool()> &pred, int ms)
{
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds(ms);
    while (std::chrono::steady_clock::now() < deadline)
    {
        QApplication::processEvents(QEventLoop::AllEvents, 1);
        if (pred())
            return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    return pred();
}

bool waitLodReady(ImageViewer &viewer, const QString &path)
{
    return waitTrue([&] { return viewer.currentPath() == path && viewer.isLodDisplay(); },
                    kReadyMs);
}

bool writeJpeg(const QString &path, int width, int height, const QColor &color, bool gradient)
{
    QImage img(width, height, QImage::Format_RGB888);
    if (img.isNull())
        return false;
    if (!gradient)
    {
        img.fill(color);
    }
    else
    {
        for (int y = 0; y < height; ++y)
        {
            uchar *row = img.scanLine(y);
            for (int x = 0; x < width; ++x)
            {
                const int i = x * 3;
                row[i] = static_cast<uchar>(x + y);
                row[i + 1] = static_cast<uchar>(x * 3 + color.green());
                row[i + 2] = static_cast<uchar>(y + color.blue());
            }
        }
    }
    return img.save(path, "JPEG", 90);
}

std::optional<QueueDecodeSample> firstFinished(QueueDecodeRole role, const QString &path)
{
    const std::string key = path.toStdString();
    for (const auto &sample : queueDecodeSamples())
    {
        if (sample.role == role && sample.path == key && sample.finished)
            return sample;
    }
    return std::nullopt;
}

bool workerStarted(QueueDecodeRole role, const QString &path)
{
    const std::string key = path.toStdString();
    for (const auto &sample : queueDecodeSamples())
    {
        if (sample.role == role && sample.path == key && sample.started)
            return true;
    }
    return false;
}

void printSample(const char *label, const QueueDecodeSample &sample)
{
    std::printf("%s queueWaitNs=%lld decodeNs=%lld postCancelNs=%lld cancelled=%d\n", label,
                static_cast<long long>(sample.queueWaitNs), static_cast<long long>(sample.decodeNs),
                static_cast<long long>(sample.postCancelNs), sample.cancelled ? 1 : 0);
}

void showViewer(ImageViewer &viewer)
{
    viewer.resize(1280, 800);
    viewer.show();
    pump(50);
}

int defaultThumbnailThreads()
{
    // Same formula as TaskScheduler's constructor for Priority::Thumbnail.
    return std::max(2, QThread::idealThreadCount() / 2);
}

class ThumbnailWidthPin
{
  public:
    explicit ThumbnailWidthPin(int n) : restore_(defaultThumbnailThreads())
    {
        TaskScheduler::instance().setQueueMaxThreads(TaskScheduler::Priority::Thumbnail, n);
    }
    ~ThumbnailWidthPin()
    {
        TaskScheduler::instance().setQueueMaxThreads(TaskScheduler::Priority::Thumbnail, restore_);
    }
    ThumbnailWidthPin(const ThumbnailWidthPin &) = delete;
    ThumbnailWidthPin &operator=(const ThumbnailWidthPin &) = delete;

  private:
    int restore_;
};

class ThumbnailHold
{
  public:
    ThumbnailHold()
    {
        handle_ = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Thumbnail,
            [this](const TaskScheduler::TaskContext &)
            {
                entered_.store(true, std::memory_order_release);
                while (!release_.load(std::memory_order_acquire))
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
                finished_.store(true, std::memory_order_release);
            },
            {}, std::chrono::steady_clock::time_point::max(), [] {});
    }
    ~ThumbnailHold()
    {
        release();
        waitFinished();
    }
    ThumbnailHold(const ThumbnailHold &) = delete;
    ThumbnailHold &operator=(const ThumbnailHold &) = delete;

    bool waitEntered()
    {
        return waitTrue([this] { return entered_.load(std::memory_order_acquire); }, kReadyMs);
    }
    void release()
    {
        release_.store(true, std::memory_order_release);
    }
    void waitFinished()
    {
        waitTrue([this] { return finished_.load(std::memory_order_acquire) || !handle_; },
                 kReadyMs);
    }

  private:
    std::atomic<bool> release_{false};
    std::atomic<bool> entered_{false};
    std::atomic<bool> finished_{false};
    TaskScheduler::TaskHandle handle_;
};

size_t thumbnailQueueDepth()
{
    return TaskScheduler::instance().metrics(TaskScheduler::ThumbnailPool).queue_depth;
}

void testIdle(const QString &path)
{
    resetQueueDecodeSamples();
    ImageViewer viewer;
    showViewer(viewer);
    viewer.setBrowseSequence({path});
    viewer.setImage(path);
    CHECK(waitLodReady(viewer, path), "idle: first LOD ready");
    const auto sample = firstFinished(QueueDecodeRole::CurrentDisplay, path);
    CHECK(sample.has_value(), "idle: current sample recorded");
    if (!sample)
        return;
    printSample("idle", *sample);
    CHECK(sample->decodeNs > 0, "idle: decode duration > 0");
    CHECK(sample->queueWaitNs >= 0, "idle: queue wait recorded");
    CHECK(sample->postCancelNs == 0, "idle: not cancelled");
    const auto totals = queueDecodeTotals(QueueDecodeRole::CurrentDisplay);
    CHECK(totals.samples >= 1 && totals.decodeNs >= sample->decodeNs, "idle: decode accumulator");
}

void testForcedWait(const QString &path)
{
    ThumbnailWidthPin pin(1);
    ImageViewer viewer;
    showViewer(viewer);
    ThumbnailHold hold;
    CHECK(hold.waitEntered(), "forced: thumbnail worker occupied");
    resetQueueDecodeSamples();
    viewer.setBrowseSequence({path});
    const size_t queuedBefore = thumbnailQueueDepth();
    viewer.setImage(path);
    const bool queued = waitTrue([&] { return thumbnailQueueDepth() > queuedBefore; }, kReadyMs);
    CHECK(queued, "forced: first LOD waited in the thumbnail queue");
    CHECK(!firstFinished(QueueDecodeRole::CurrentDisplay, path).has_value(),
          "forced: decode has not started while the worker is held");
    hold.release();
    CHECK(waitLodReady(viewer, path), "forced: LOD ready after the queue wait");
    hold.waitFinished();
    const auto sample = firstFinished(QueueDecodeRole::CurrentDisplay, path);
    CHECK(sample.has_value(), "forced: current sample recorded");
    if (!sample)
        return;
    printSample("forced", *sample);
    CHECK(sample->queueWaitNs > 0, "forced: queue wait > 0");
    CHECK(sample->decodeNs > 0, "forced: decode counted separately");
    CHECK(sample->postCancelNs == 0, "forced: queue wait is not post-cancel compute");
    CHECK(sample->postCancelNs <= sample->decodeNs, "forced: post-cancel is within decode");
}

void testSlowThenFast(const QString &slow, const QString &fast)
{
    resetQueueDecodeSamples();
    ImageViewer viewer;
    showViewer(viewer);
    viewer.setBrowseSequence({slow});
    viewer.setImage(slow);
    CHECK(waitTrue([&] { return workerStarted(QueueDecodeRole::CurrentDisplay, slow); }, kReadyMs),
          "switch: slow LOD worker started");
    viewer.setImage(fast);
    const bool ready = waitTrue(
        [&]
        {
            return viewer.currentPath() == fast && viewer.isLodDisplay() &&
                   firstFinished(QueueDecodeRole::CurrentDisplay, slow).has_value() &&
                   firstFinished(QueueDecodeRole::CurrentDisplay, fast).has_value();
        },
        kReadyMs);
    CHECK(ready, "switch: fast image ready and both samples finished");
    const auto slowSample = firstFinished(QueueDecodeRole::CurrentDisplay, slow);
    const auto fastSample = firstFinished(QueueDecodeRole::CurrentDisplay, fast);
    CHECK(slowSample.has_value() && fastSample.has_value(), "switch: both images have samples");
    if (!slowSample || !fastSample)
        return;
    printSample("slow", *slowSample);
    printSample("fast", *fastSample);
    CHECK(fastSample->decodeNs > 0, "switch: fast decode > 0");
    CHECK(slowSample->postCancelNs >= 0, "switch: post-cancel compute recorded");
    CHECK(slowSample->postCancelNs <= slowSample->decodeNs,
          "switch: leftover decode is not queue wait");
    if (slowSample->cancelled && slowSample->decodeNs > 0)
        CHECK(slowSample->postCancelNs > 0, "switch: cancelled decode kept leftover compute");
    const auto latestFast =
        latestQueueDecodeSample(QueueDecodeRole::CurrentDisplay, fast.toStdString());
    CHECK(latestFast.has_value(), "switch: latest fast sample");
}

void testNeighbor(const QString &first, const QString &second)
{
    resetQueueDecodeSamples();
    ImageViewer viewer;
    showViewer(viewer);
    viewer.setBrowseSequence({first});
    viewer.setImage(first);
    viewer.setBrowseSequence({first, second});
    const bool ready = waitTrue(
        [&]
        {
            return viewer.currentPath() == first && viewer.isLodDisplay() &&
                   firstFinished(QueueDecodeRole::NeighborPreload, second).has_value();
        },
        kReadyMs);
    CHECK(ready, "neighbor: preload sample landed and current LOD ready");
    const auto current = firstFinished(QueueDecodeRole::CurrentDisplay, first);
    const auto neighbor = firstFinished(QueueDecodeRole::NeighborPreload, second);
    CHECK(current.has_value(), "neighbor: current sample");
    CHECK(neighbor.has_value(), "neighbor: preload sample");
    if (current)
    {
        printSample("neighbor-current", *current);
        CHECK(current->decodeNs > 0, "neighbor: current decode > 0");
    }
    if (neighbor)
    {
        printSample("neighbor-preload", *neighbor);
        CHECK(neighbor->decodeNs > 0, "neighbor: preload decode > 0");
        CHECK(neighbor->queueWaitNs >= 0, "neighbor: queue wait recorded");
        CHECK(neighbor->postCancelNs <= neighbor->decodeNs, "neighbor: post-cancel within decode");
    }
    const auto totals = queueDecodeTotals(QueueDecodeRole::NeighborPreload);
    CHECK(totals.samples >= 1 && totals.decodeNs > 0, "neighbor: preload accumulator");
}

} // namespace

int main(int argc, char **argv)
{
    QApplication app(argc, argv);
    QTemporaryDir tmp;
    if (!tmp.isValid())
    {
        std::printf("FAIL: temp dir\n");
        return 1;
    }
    const QString fast = tmp.filePath(QStringLiteral("fast.jpg"));
    const QString slow = tmp.filePath(QStringLiteral("slow.jpg"));
    const QString neighbor = tmp.filePath(QStringLiteral("neighbor.jpg"));
    CHECK(writeJpeg(fast, kFastW, kFastH, QColor(30, 140, 200), false), "fast jpeg");
    CHECK(writeJpeg(slow, kSlowW, kSlowH, QColor(180, 40, 40), true), "slow jpeg");
    CHECK(writeJpeg(neighbor, kFastW, kFastH, QColor(40, 160, 60), false), "neighbor jpeg");
    if (g_failures != 0)
        return 1;

    // Pin the thumbnail pool before any other case has created its threads.
    testForcedWait(fast);
    testIdle(fast);
    testSlowThenFast(slow, fast);
    testNeighbor(fast, neighbor);

    if (g_failures == 0)
        std::printf("OK: queue_decode_timing\n");
    else
        std::printf("%d FAIL(s) in queue_decode_timing\n", g_failures);
    std::fflush(stdout);
    return g_failures == 0 ? 0 : 1;
}
