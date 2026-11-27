#pragma once

// Full-resolution "export this pane's tuning result" job.
//
// CompareWorkspace owns the UI (button, cell context menu, save dialog) and knows
// which image and which adjustments apply; this object owns everything that makes
// the export asynchronous - the cancellation flag, the decode request handle, the
// worker task and the generation counter that suppresses stale completions. That
// used to be four members plus three methods inside the workspace header, and the
// whole pipeline lived in the same TU as the sliders that trigger it.
//
// Threading: every worker callback carries its state BY VALUE (shared block,
// generation, request). Nothing on a worker touches `this`; the only thing that
// reaches the owning thread is a `mvcore::postToMainThread` hop, and the shared
// block's `alive` flag plus the generation counter decide whether the delivery
// still applies. Same discipline as PythonScriptRunner.

#include <QObject>
#include <QString>

#include "application/ImageLoadingService.h"
#include "core/async/AsyncLifetimeToken.h"
#include "core/image/CellImageAdjust.h"
#include "core/image/ImageBuffer.h"
#include "core/scheduler/TaskScheduler.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>
#include <string>

namespace mviewer::ui
{

class CompareAdjustedExportJob final : public QObject
{
    Q_OBJECT

  public:
    using StatusFn = std::function<void(const QString &, int timeoutMs)>;

    /// `lifetime` is the consumer token that suppresses deliveries after the
    /// workspace dies; `status` is invoked on the owning thread.
    CompareAdjustedExportJob(QObject *parent,
                             std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime,
                             StatusFn status);
    ~CompareAdjustedExportJob() override;

    void cancel();

    /// Decode the source at full resolution, apply `adjust`, write `outPath`.
    /// Safe to call again: any previous attempt is cancelled first.
    void start(const std::string &sourcePath, int frameIndex, const QString &outPath,
               const mviewer::core::CellImageAdjust &adjust);

  private:
    struct Request
    {
        mviewer::core::CellImageAdjust adjust;
        std::string outPath;
        std::shared_ptr<std::atomic<bool>> cancel;
    };

    /// State shared with worker callbacks. Owns no QObject, so workers may hold
    /// it safely after the job itself is gone.
    struct Shared
    {
        std::atomic<bool> alive{true};
        std::atomic<uint64_t> generation{0};
        StatusFn status;
        std::function<void(TaskScheduler::TaskHandle)> storeTask; // owner thread only
    };

    /// Runs on the Analysis pool; touches nothing but its arguments and delivers
    /// through the owning thread's dispatcher.
    static void finishAdjustAndEncode(const Request &request, ImageData pixels,
                                      const TaskScheduler::TaskContext &context,
                                      uint64_t generation, const std::shared_ptr<Shared> &shared);

    void launch(std::string sourcePath, int frameIndex, Request request);

    std::weak_ptr<mviewer::core::AsyncLifetimeToken> m_lifetime;
    std::shared_ptr<Shared> m_shared;
    uint64_t m_generation = 0; // owner thread only (mirrors shared->generation)
    std::shared_ptr<std::atomic<bool>> m_cancel;
    mviewer::application::ImageLoadingService::AsyncRequestHandle m_handle;
    TaskScheduler::TaskHandle m_task; // owner thread only
};

} // namespace mviewer::ui
