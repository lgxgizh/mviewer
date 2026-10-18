#include "core/render/DisplayResampleDetail.h"
#include <algorithm>

#include "core/scheduler/TaskScheduler.h"

#include <QCoreApplication>
#include <QDebug>
#include <QRunnable>
#include <QThread>
#include <QThreadPool>

#include <condition_variable>
#include <exception>
#include <mutex>
#include <utility>

namespace mviewer::core::resample_detail
{
namespace
{

struct Latch
{
    int left = 0;
    std::mutex mu;
    std::condition_variable cv;

    void set(int count)
    {
        left = count;
    }

    void done()
    {
        std::lock_guard<std::mutex> lock(mu);
        --left;
        if (left == 0)
            cv.notify_all();
    }

    void wait()
    {
        std::unique_lock<std::mutex> lock(mu);
        cv.wait(lock, [this]() { return left == 0; });
    }
};

class BandRunnable : public QRunnable
{
  public:
    BandRunnable(std::function<void()> fn, Latch *latch) : fn_(std::move(fn)), latch_(latch)
    {
        setAutoDelete(true);
    }

    void run() override
    {
        // Count down even when fn_ throws, and do not let the exception reach
        // QThreadPool — an exception leaving a pool thread calls std::terminate.
        struct Guard
        {
            Latch *latch;
            ~Guard()
            {
                latch->done();
            }
        } guard{latch_};
        try
        {
            fn_();
        }
        catch (const std::exception &error)
        {
            qWarning("DisplayResample band failed: %s", error.what());
        }
        catch (...)
        {
            qWarning("DisplayResample band failed: unknown error");
        }
    }

  private:
    std::function<void()> fn_;
    Latch *latch_ = nullptr;
};

QThreadPool *resamplePool()
{
    // Leaked on purpose. QThreadPool joins workers in its destructor, which is
    // unsafe after QCoreApplication is gone (ADR 017). The function-local
    // static initializes this pointer once, including when the first callers
    // race. Match TaskScheduler UI budget: HQ resample is bursty foreground
    // work and used to stack another full-core pool on top of Decode (≈30
    // runnable threads on 8 cores). Half the cores keeps Sharp usable without
    // starving decode/thumbnails.
    static QThreadPool *const pool = []()
    {
        auto *created = new QThreadPool();
        int threads = QThread::idealThreadCount();
        if (threads < 1)
            threads = 1;
        threads = std::max(2, threads / 2);
        created->setMaxThreadCount(threads);
        created->setExpiryTimeout(-1);
        return created;
    }();
    return pool;
}

int bandRows(int rowCount, int bands, int index, int cursor)
{
    const int remain = bands - index;
    if (remain <= 1)
        return rowCount - cursor;
    int rows = (rowCount - cursor) / remain;
    if (rows < 1)
        rows = 1;
    return rows;
}

} // namespace

void parallelForRows(int rowCount, int minRows, const std::atomic<bool> *cancel,
                     const std::function<void(int, int)> &fn)
{
    if (rowCount <= 0 || resampleCancelled(cancel))
        return;
    const bool serial = rowCount < minRows || QCoreApplication::instance() == nullptr;
    int threads = 1;
    if (!serial)
        threads = resamplePool()->maxThreadCount();
    if (serial || threads <= 1)
    {
        fn(0, rowCount);
        return;
    }

    // A multi-band resample is foreground work (HQ zoom / compare panes):
    // thumbnail tasks back off while it runs.
    const TaskScheduler::ForegroundWork foreground;
    int bands = threads;
    if (bands > rowCount)
        bands = rowCount;
    Latch latch;
    latch.set(bands);
    int cursor = 0;
    for (int band = 0; band < bands; ++band)
    {
        const int rows = bandRows(rowCount, bands, band, cursor);
        const int y0 = cursor;
        const int y1 = cursor + rows;
        cursor = y1;
        auto *job = new BandRunnable(
            [&fn, cancel, y0, y1]()
            {
                if (!resampleCancelled(cancel))
                    fn(y0, y1);
            },
            &latch);
        resamplePool()->start(job);
    }
    latch.wait();
}

} // namespace mviewer::core::resample_detail
