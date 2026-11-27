#include "compareworkspace_adjusted_export.h"

#include "core/MainThreadDispatcher.h"
#include "core/image/Encoder.h"
#include "core/image/ImageFrame.h"

#include <QFileInfo>

namespace mviewer::ui
{
namespace
{
bool cancelled(const std::shared_ptr<std::atomic<bool>> &cancel,
               const TaskScheduler::TaskContext *context)
{
    if (cancel && cancel->load(std::memory_order_relaxed))
        return true;
    return context != nullptr && context->isCancelled();
}

QString exportedName(const std::string &outPath)
{
    return QFileInfo(QString::fromUtf8(outPath.c_str())).fileName();
}
} // namespace

void CompareAdjustedExportJob::finishAdjustAndEncode(
    const Request &request, ImageData pixels, const TaskScheduler::TaskContext &context,
    uint64_t generation, const std::shared_ptr<Shared> &shared)
{
    const auto stopped = [&]() { return cancelled(request.cancel, &context); };
    if (stopped())
        return;
    const ImageData out = mviewer::core::applyCellImageAdjust(pixels, request.adjust, stopped);
    QString failure;
    if (stopped())
        return;
    if (out.isNull())
        failure = QStringLiteral("Export failed: adjustment processing failed");
    else if (!Encoder::encode(out, request.outPath))
        failure = QStringLiteral("Export failed: cannot write file");
    if (stopped())
        return;
    const QString text =
        failure.isEmpty()
            ? QStringLiteral("Tuning result exported: %1").arg(exportedName(request.outPath))
            : failure;
    const int msec = failure.isEmpty() ? 5000 : 6000;
    mvcore::postToMainThread([shared, generation, text, msec]()
                             {
                                 if (!shared->alive.load(std::memory_order_acquire) ||
                                     shared->generation.load(std::memory_order_acquire) != generation)
                                     return;
                                 if (shared->status)
                                     shared->status(text, msec);
                             });
}

CompareAdjustedExportJob::CompareAdjustedExportJob(
    QObject *parent, std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime, StatusFn status)
    : QObject(parent), m_lifetime(std::move(lifetime)), m_shared(std::make_shared<Shared>())
{
    m_shared->status = std::move(status);
    // Only ever invoked from the owning thread, and only while `alive` is set.
    m_shared->storeTask = [this](TaskScheduler::TaskHandle task) { m_task = std::move(task); };
}

CompareAdjustedExportJob::~CompareAdjustedExportJob()
{
    cancel();
    m_shared->alive.store(false, std::memory_order_release);
    m_shared->status = nullptr;
    m_shared->storeTask = nullptr;
}

void CompareAdjustedExportJob::cancel()
{
    ++m_generation;
    m_shared->generation.store(m_generation, std::memory_order_release);
    if (m_cancel)
        m_cancel->store(true, std::memory_order_relaxed);
    if (m_task)
        TaskScheduler::cancel(m_task);
    m_task.reset();
    if (m_handle)
        mviewer::application::ImageLoadingService::instance().cancelAsync(m_handle);
}

void CompareAdjustedExportJob::start(const std::string &sourcePath, int frameIndex,
                                     const QString &outPath,
                                     const mviewer::core::CellImageAdjust &adjust)
{
    cancel(); // a second export supersedes the first
    Request request;
    request.adjust = adjust;
    request.outPath = outPath.toUtf8().toStdString();
    request.cancel = std::make_shared<std::atomic<bool>>(false);
    launch(sourcePath, frameIndex, std::move(request));
}

void CompareAdjustedExportJob::launch(std::string sourcePath, int frameIndex, Request request)
{
    m_generation = m_shared->generation.load(std::memory_order_acquire);
    m_cancel = request.cancel;
    const auto shared = m_shared;
    const uint64_t generation = m_generation;
    const ImageLoadOptions opts{true, false, 256};
    m_handle = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        sourcePath, frameIndex,
        [shared, request, generation](const mviewer::application::ImageLoadingService::Result &res)
        {
            // Repository/worker thread: captured values only.
            if (!shared->alive.load(std::memory_order_acquire) ||
                cancelled(request.cancel, nullptr))
                return;
            const bool decoded = res.success() && res.frame && !res.frame->pixels().isNull();
            const ImageData pixels = decoded ? res.frame->pixels() : ImageData{};
            // Everything below needs the owning thread, so hop once.
            mvcore::postToMainThread(
                [shared, request, pixels, generation, decoded]()
                {
                    if (!shared->alive.load(std::memory_order_acquire) ||
                        shared->generation.load(std::memory_order_acquire) != generation)
                        return;
                    if (!decoded)
                    {
                        if (shared->status)
                            shared->status(QStringLiteral("Export failed: unable to decode the "
                                                         "full-resolution image"),
                                           6000);
                        return;
                    }
                    TaskScheduler::TaskHandle task = TaskScheduler::instance().submit(
                        TaskScheduler::Priority::Analysis,
                        [request, pixels, generation, shared](const TaskScheduler::TaskContext &ctx)
                        { finishAdjustAndEncode(request, pixels, ctx, generation, shared); });
                    if (!task)
                    {
                        if (shared->status)
                            shared->status(
                                QStringLiteral("Export failed: adjustment processing failed"), 6000);
                        return;
                    }
                    // Registering the handle mutates job state, so it stays here on
                    // the owning thread - dropped when superseded meanwhile.
                    if (!shared->alive.load(std::memory_order_acquire) ||
                        shared->generation.load(std::memory_order_acquire) != generation)
                    {
                        TaskScheduler::cancel(task);
                        return;
                    }
                    if (shared->storeTask)
                        shared->storeTask(std::move(task));
                });
        },
        opts, m_lifetime);
    if (!m_handle)
        mvcore::postToMainThread(
            [shared, generation]()
            {
                if (!shared->alive.load(std::memory_order_acquire) ||
                    shared->generation.load(std::memory_order_acquire) != generation)
                    return;
                if (shared->status)
                    shared->status(QStringLiteral("Export failed: unable to decode the "
                                                 "full-resolution image"),
                                   6000);
            });
}

} // namespace mviewer::ui
