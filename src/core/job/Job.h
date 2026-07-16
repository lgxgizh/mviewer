#pragma once

#include "core/scheduler/TaskScheduler.h"

#include <chrono>
#include <functional>
#include <memory>
#include <string>
#include <vector>

// Job system: a thin, unified facade over TaskScheduler so that decode,
// thumbnail, analysis, and IO work can all be expressed as the same `Job`
// concept (Task -> Scheduler -> Worker -> Cancellation -> Dependency ->
// Progress), instead of three ad-hoc submit() overloads.
//
// This is intentionally a *facade*, not a rewrite: the existing DecodePool /
// ThumbnailPool / AnalysisPool paths keep working; new code can describe work
// as a Job and submit it through JobSystem, gaining cancellation, progress,
// dependency ordering, and metrics for free. No ABI is frozen here.

namespace mviewer::core
{

// A unit of asynchronous work. Value type; the scheduler owns the live
// TaskContext behind a shared JobHandle.
struct Job
{
    std::string name;
    TaskScheduler::Priority priority = TaskScheduler::Priority::Background;
    TaskScheduler::PoolType pool = TaskScheduler::PoolType::MetadataPool;

    // The work itself. Receives the live TaskContext so it can honour
    // cancellation and report progress.
    std::function<void(const TaskScheduler::TaskContext&)> work;

    // Called when the work completes (success or after cancellation).
    std::function<void()> done;

    // Progress callback (0..100).
    std::function<void(int)> onProgress;

    // Jobs that must finish before this one starts.
    std::vector<TaskScheduler::TaskId> dependsOn;

    // Optional deadline; expired jobs are skipped / reported.
    std::chrono::steady_clock::time_point deadline =
        std::chrono::steady_clock::time_point::max();
};

// Shared handle to a submitted job: cancel, inspect progress, query id.
using JobHandle = TaskScheduler::TaskHandle;

class JobSystem
{
  public:
    static JobSystem& instance()
    {
        static JobSystem inst;
        return inst;
    }

    // Submit a Job. Returns a handle (nullptr if the pool rejected it under
    // back-pressure / pause).
    JobHandle submit(const Job& job)
    {
        return TaskScheduler::instance().submit(
            job.priority, job.work, job.dependsOn, job.deadline, job.done, job.onProgress);
    }

    // Convenience: submit with an explicit pool (priority derived from pool).
    JobHandle submitOnPool(const Job& job)
    {
        Job j = job;
        j.priority = TaskScheduler::toPriority(job.pool);
        return submit(j);
    }

    static void cancel(JobHandle& h) { TaskScheduler::cancel(h); }
    static void cancelTree(TaskScheduler::TaskId root) { TaskScheduler::cancelTree(root); }

    TaskScheduler::PoolMetrics metrics(TaskScheduler::PoolType pool) const
    {
        return TaskScheduler::instance().metrics(pool);
    }

    void shutdown(std::chrono::milliseconds timeout = std::chrono::seconds(5))
    {
        TaskScheduler::instance().shutdown(timeout);
    }

  private:
    JobSystem() = default;
};

} // namespace mviewer::core
