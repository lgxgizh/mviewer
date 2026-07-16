// Job system tests: unified Task -> Scheduler -> cancel + progress + deps.
#include "core/job/Job.h"

#include <atomic>
#include <chrono>
#include <iostream>
#include <mutex>
#include <thread>
#include <vector>

#define CHECK(cond, msg)                               \
    do                                                 \
    {                                                  \
        if (!(cond))                                   \
        {                                              \
            std::cerr << "FAIL: " << msg << std::endl; \
            return 1;                                  \
        }                                              \
        else                                           \
        {                                              \
            std::cout << "PASS: " << msg << std::endl; \
        }                                              \
    } while (0)

using namespace mviewer::core;

int main()
{
    std::cout << "[Job system tests]\n";
    auto& js = JobSystem::instance();
    auto& sched = TaskScheduler::instance();

    // 1) Progress callbacks fire and the handle reports the latest value.
    {
        std::atomic<int> last{0};
        std::atomic<bool> done{false};
        Job job;
        job.name = "progress-job";
        job.priority = TaskScheduler::Priority::Background;
        job.work = [&](const TaskScheduler::TaskContext& ctx) {
            for (int i = 1; i <= 10; ++i)
            {
                if (ctx.isCancelled())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(2));
                const_cast<TaskScheduler::TaskContext&>(ctx).reportProgress(i * 10);
            }
        };
        job.onProgress = [&](int p) { last = p; };
        job.done = [&]() { done = true; };

        auto h = js.submit(job);
        CHECK(h != nullptr, "submit returns a handle");
        // Wait for completion (job is ~20ms).
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CHECK(done.load(), "done callback fired");
        CHECK(last.load() == 100, "progress reached 100");
        CHECK(h->currentProgress() == 100, "handle reports progress 100");
    }

    // 2) Cancellation: a long job cancelled early stops its work early.
    {
        std::atomic<int> iterations{0};
        std::atomic<int> progress{0};
        Job job;
        job.name = "cancel-job";
        job.priority = TaskScheduler::Priority::Background;
        job.work = [&](const TaskScheduler::TaskContext& ctx) {
            for (int i = 1; i <= 100; ++i)
            {
                if (ctx.isCancelled())
                    return;
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
                ++iterations;
                const_cast<TaskScheduler::TaskContext&>(ctx).reportProgress(i);
            }
        };
        job.onProgress = [&](int p) { progress = p; };

        auto h = js.submit(job);
        std::this_thread::sleep_for(std::chrono::milliseconds(60));
        JobSystem::cancel(h);
        std::this_thread::sleep_for(std::chrono::milliseconds(80));
        CHECK(h->isCancelled(), "handle reports cancelled");
        CHECK(iterations.load() < 100, "cancelled job stopped its work early");
        std::cout << "  iterations before cancel: " << iterations.load() << std::endl;
    }

    // 3) Dependency ordering: dependent job runs only after its parent.
    {
        std::vector<std::string> order;
        std::mutex m;
        TaskScheduler::TaskId parentId = 0;
        Job parent;
        parent.name = "parent";
        parent.priority = TaskScheduler::Priority::Background;
        parent.work = [&](const TaskScheduler::TaskContext& ctx) {
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
        child.work = [&](const TaskScheduler::TaskContext&) {
            std::lock_guard<std::mutex> l(m);
            order.push_back("child");
        };
        js.submit(child);

        std::this_thread::sleep_for(std::chrono::milliseconds(120));
        CHECK(!order.empty() && order[0] == "parent",
              "parent ran before child (dependency honored)");
        CHECK(order.size() == 2 && order[1] == "child", "child ran after parent");
    }

    std::cout << "Job system tests done\n";
    return 0;
}
