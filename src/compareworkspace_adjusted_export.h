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
// Lifecycle: create once per workspace, call start() for each export, cancel()
// before starting another one or when the session ends.

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

    void launch(std::string sourcePath, int frameIndex, Request request);
    void finish(ImageData pixels, Request request, const TaskScheduler::TaskContext &context);
    void postStatus(const QString &text, int timeoutMs);

    std::weak_ptr<mviewer::core::AsyncLifetimeToken> m_lifetime;
    StatusFn m_status;
    uint64_t m_generation = 0;
    std::shared_ptr<std::atomic<bool>> m_cancel;
    mviewer::application::ImageLoadingService::AsyncRequestHandle m_handle;
    TaskScheduler::TaskHandle m_task;
};

} // namespace mviewer::ui
