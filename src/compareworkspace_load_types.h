#pragma once

#include "application/ImageLoadingService.h"
#include "core/scheduler/TaskScheduler.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <vector>

class ImageFrame;

// Shared by CompareWorkspace load TUs. Kept out of compareworkspace.h so the
// public header stays under the Strict 800-line complexity gate.
struct CompareLoadRequest
{
    std::atomic<bool> accounted{false};
    // Probe + foreground load share handlesMutex with cancelLoadBatch.
    TaskScheduler::TaskHandle probeHandle;
    mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
};

struct CompareLoadBatch
{
    uint64_t generation = 0;
    std::shared_ptr<std::vector<std::shared_ptr<ImageFrame>>> frames;
    std::shared_ptr<std::atomic<int>> remaining;
    std::shared_ptr<std::atomic<int>> failed;
    std::shared_ptr<std::atomic<int>> infeasible;
    std::vector<std::unique_ptr<CompareLoadRequest>> requests;
    std::mutex handlesMutex;
};
