#include "compareworkspace_adjusted_export.h"

#include "core/image/CellImageAdjust.h"
#include "core/image/Encoder.h"
#include "core/image/ImageFrame.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QPointer>
#include <QThread>

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
} // namespace

CompareAdjustedExportJob::CompareAdjustedExportJob(
    QObject *parent, std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime, StatusFn status)
    : QObject(parent), m_lifetime(std::move(lifetime)), m_status(std::move(status))
{
}

CompareAdjustedExportJob::~CompareAdjustedExportJob()
{
    cancel();
}

void CompareAdjustedExportJob::postStatus(const QString &text, int timeoutMs)
{
    // The adjust stage runs on a worker thread; the workspace draws from the
    // status callback, so hop to this object's own thread first.
    if (QThread::currentThread() == thread())
    {
        if (m_status)
            m_status(text, timeoutMs);
        return;
    }
    const QPointer<CompareAdjustedExportJob> guard(this);
    const uint64_t generation = m_generation;
    QMetaObject::invokeMethod(
        this,
        [this, guard, generation, text, timeoutMs]()
        {
            if (!guard || generation != m_generation || !m_status)
                return;
            m_status(text, timeoutMs);
        },
        Qt::QueuedConnection);
}

void CompareAdjustedExportJob::cancel()
{
    ++m_generation;
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
    const uint64_t generation = m_generation;
    m_cancel = request.cancel;
    const QPointer<CompareAdjustedExportJob> guard(this);
    const ImageLoadOptions opts{true, false, 256};
    m_handle = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
        sourcePath, frameIndex,
        [this, guard, generation, request](
            const mviewer::application::ImageLoadingService::Result &res)
        {
            if (!guard || !qApp || cancelled(request.cancel, nullptr))
                return;
            const bool decoded = res.success() && res.frame && !res.frame->pixels().isNull();
            const ImageData pixels = decoded ? res.frame->pixels() : ImageData{};
            // Everything below touches job members, so it happens on the owning
            // thread rather than inside the repository's completion.
            QMetaObject::invokeMethod(
                this,
                [this, guard, generation, request, decoded, pixels]() mutable
                {
                    if (!guard || generation != m_generation)
                        return;
                    if (!decoded)
                    {
                        postStatus(QStringLiteral("Export failed: unable to decode the "
                                                 "full-resolution image"),
                                   6000);
                        return;
                    }
                    TaskScheduler::TaskHandle task = TaskScheduler::instance().submit(
                        TaskScheduler::Priority::Analysis,
                        [this, guard, generation, pixels, request](
                            const TaskScheduler::TaskContext &ctx)
                        { finish(pixels, request, ctx); });
                    if (!task)
                    {
                        postStatus(QStringLiteral("Export failed: adjustment processing failed"),
                                   6000);
                        return;
                    }
                    m_task = std::move(task);
                },
                Qt::QueuedConnection);
        },
        opts, m_lifetime);
    if (!m_handle)
        postStatus(QStringLiteral("Export failed: unable to decode the full-resolution image"),
                   6000);
}

void CompareAdjustedExportJob::finish(ImageData pixels, Request request,
                                      const TaskScheduler::TaskContext &context)
{
    const auto stopped = [&]() { return cancelled(request.cancel, &context); };
    if (stopped())
        return;
    const ImageData out = mviewer::core::applyCellImageAdjust(pixels, request.adjust, stopped);
    if (stopped())
        return;
    if (out.isNull())
    {
        postStatus(QStringLiteral("Export failed: adjustment processing failed"), 6000);
        return;
    }
    if (!stopped() && !Encoder::encode(out, request.outPath))
    {
        postStatus(QStringLiteral("Export failed: cannot write file"), 6000);
        return;
    }
    if (stopped())
        return;
    const QString name = QFileInfo(QString::fromUtf8(request.outPath.c_str())).fileName();
    postStatus(QStringLiteral("Tuning result exported: %1").arg(name), 5000);
}

} // namespace mviewer::ui
