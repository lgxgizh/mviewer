// Job system tests: unified Task -> Scheduler -> cancel + progress + deps.
#include "core/job/Job.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#define CHECK(cond, msg)                                                                           \
    do                                                                                             \
    {                                                                                              \
        if (!(cond))                                                                               \
        {                                                                                          \
            std::cerr << "FAIL: " << msg << '\n';                                                  \
            return 1;                                                                              \
        }                                                                                          \
        else                                                                                       \
        {                                                                                          \
            std::cout << "PASS: " << msg << '\n' << std::flush;                                    \
        }                                                                                          \
    } while (0)

using namespace mviewer::core;

int main()
{
    std::cout << "[Job system tests]\n";
    auto &js = JobSystem::instance();
    auto &sched = TaskScheduler::instance();

    // 1) Progress callbacks fire and the handle reports the latest value.
    {
        std::atomic<int> last{0};
        std::atomic<bool> done{false};
        Job job;
        job.name = "progress-job";
        job.priority = TaskScheduler::Priority::Background;
        job.work = [&](const TaskScheduler::TaskContext &ctx)
        {
            for (int i = 1; i <= 10; ++i)
            {
                if (ctx.isCancelled())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                const_cast<TaskScheduler::TaskContext &>(ctx).reportProgress(i * 10);
            }
        };
        job.onProgress = [&](int p) { last = p; };
        job.done = [&]() { done = true; };

        auto h = js.submit(job);
        CHECK(h != nullptr, "submit returns a handle");
        // Wait (poll) until the done callback fires or a generous timeout elapses.
        // A fixed sleep is racy under ctest concurrency where the Background
        // pool's worker thread may not start the task within a short window.
        {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (!done.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(done.load(), "done callback fired");
        CHECK(last.load() == 100, "progress reached 100");
        CHECK(h->currentProgress() == 100, "handle reports progress 100");
    }

    // 2) Cancellation: the job loops until the flag is set, or until a hard
    //    bound (~1 s). Ignoring the flag runs that bound out. The old check
    //    waited ~140 ms against 100 x 5 ms of work, so iterations < 100 was
    //    true whether or not cancel stopped the loop. Poll until the worker
    //    is idle (the bound fits inside the deadline), then require an early
    //    stop and a cancelled handle.
    {
        constexpr int kHardBound = 2000;
        std::atomic<int> iterations{0};
        std::atomic<bool> stopped{false};
        Job job;
        job.name = "cancel-job";
        job.priority = TaskScheduler::Priority::Background;
        job.work = [&](const TaskScheduler::TaskContext &ctx)
        {
            for (int i = 0; i < kHardBound; ++i)
            {
                if (ctx.isCancelled())
                    break;
                // Steady-clock slice, not sleep_for(1ms): the Windows tick is
                // often ~15 ms, which would push an ignored cancel past the
                // idle poll. yield() lets the test thread cancel on one core.
                const auto sliceEnd =
                    std::chrono::steady_clock::now() + std::chrono::microseconds(500);
                while (std::chrono::steady_clock::now() < sliceEnd)
                {
                    if (ctx.isCancelled())
                        break;
                    std::this_thread::yield();
                }
                if (ctx.isCancelled())
                    break;
                ++iterations;
            }
            stopped.store(true);
        };

        auto h = js.submit(job);
        CHECK(h != nullptr, "cancel job returns a handle");
        {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
            while (iterations.load() == 0 && !stopped.load() &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(iterations.load() > 0, "cancel job started its work");
        const int atCancel = iterations.load();
        JobSystem::cancel(h);
        {
            const auto deadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(5000);
            while (!stopped.load() && std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        const int doneIters = iterations.load();
        CHECK(stopped.load(), "cancelled job became idle");
        CHECK(h->isCancelled(), "handle reports cancelled");
        CHECK(doneIters < kHardBound, "cancelled job stopped before the hard bound");
        // 800 steps is ~400 ms. A live cancel returns on the next slice; an
        // ignored flag runs the rest of the ~1 s bound and fails this.
        CHECK(doneIters < atCancel + 800, "cancelled job stopped soon after cancel");
        std::cout << "  iterations before cancel: " << atCancel << " at stop: " << doneIters
                  << '\n';
    }

    // 3) Dependency ordering: dependent job runs only after its parent.
    {
        std::vector<std::string> order;
        std::mutex m;
        TaskScheduler::TaskId parentId = 0;
        Job parent;
        parent.name = "parent";
        parent.priority = TaskScheduler::Priority::Background;
        parent.work = [&](const TaskScheduler::TaskContext &ctx)
        {
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            std::lock_guard<std::mutex> l(m);
            order.push_back("parent");
            parentId = ctx.id;
        };
        auto ph = js.submit(parent);

        // Submit child after we know parent id is assigned (handle exists
        // immediately, id valid before execution).
        Job child;
        child.name = "child";
        child.priority = TaskScheduler::Priority::Background;
        child.dependsOn = {ph->id};
        child.work = [&](const TaskScheduler::TaskContext &)
        {
            std::lock_guard<std::mutex> l(m);
            order.push_back("child");
        };
        js.submit(child);

        // Wait until both parent and child have run (poll, not a blind sleep,
        // to stay robust under ctest concurrency).
        {
            const auto depDeadline =
                std::chrono::steady_clock::now() + std::chrono::milliseconds(2000);
            while (order.size() < 2 && std::chrono::steady_clock::now() < depDeadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
        }
        CHECK(!order.empty() && order[0] == "parent",
              "parent ran before child (dependency honored)");
        CHECK(order.size() == 2 && order[1] == "child", "child ran after parent");
    }

    std::cout << "Job system tests done\n";
    return 0;
}
