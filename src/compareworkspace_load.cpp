#include "compareworkspace_p.h"

#include "application/ImageLoadingService.h"
#include "core/image/ImageFrame.h"
#include "core/image/SourceImage.h"

#include <functional>

namespace
{
using FinishFn = std::function<void()>;
using AccountFn = std::function<void(bool failed)>;
using ResultFn = std::function<void(const mviewer::application::ImageLoadingService::Result &)>;

void runCompareCapabilityProbe(const std::shared_ptr<CompareLoadBatch> &batch, size_t i,
                               CompareLoadRequest *request, const std::string &path, int frameIndex,
                               const ImageLoadOptions &opts,
                               const std::weak_ptr<mviewer::core::AsyncLifetimeToken> &lifetime,
                               const FinishFn &finish, const AccountFn &account,
                               const ResultFn &onResult, const TaskScheduler::TaskContext &ctx)
{
    if (ctx.isCancelled())
        return;
    try
    {
        std::shared_ptr<mviewer::core::SourceImage> source;
        try
        {
            source = mviewer::core::SourceImage::open(path);
        }
        catch (...)
        {
            if (!ctx.isCancelled())
                account(true);
            return;
        }

        std::unique_lock<std::mutex> lk(batch->handlesMutex);
        if (ctx.isCancelled())
            return;

        const qint64 pixels =
            source ? static_cast<qint64>(source->metadata().width) * source->metadata().height : 0;
        if (source && pixels > kCompareAnalysisFeasiblePixels)
        {
            batch->infeasible->fetch_add(1, std::memory_order_relaxed);
            (*batch->frames)[i] = std::make_shared<ImageFrame>(source->metadata(), ImageData());
            lk.unlock();
            account(false);
            return;
        }

        mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
        bool rejected = false;
        try
        {
            handle = mviewer::application::ImageLoadingService::instance().loadFrameAsync(
                path, frameIndex, onResult, opts, lifetime);
        }
        catch (...)
        {
            rejected = true;
        }
        request->handle = std::move(handle);
        lk.unlock();
        if (rejected)
            account(true);
    }
    catch (...)
    {
        if (!ctx.isCancelled())
            account(true);
    }
}
} // namespace

void CompareWorkspace::queueLoadRequests(const std::shared_ptr<LoadBatch> &batch,
                                         const std::vector<std::string> &paths,
                                         const std::vector<int> &frameIndices)
{
    auto self = std::make_shared<QPointer<CompareWorkspace>>(this);
    auto lifetime = m_lifetime;
    const ImageLoadOptions opts{true, false, 256};
    const auto finish = [self, batch]()
    {
        if (!qApp)
            return;
        QMetaObject::invokeMethod(
            qApp,
            [self, batch]()
            {
                CompareWorkspace *ws = self->data();
                if (!ws || batch->generation != ws->m_loadGen)
                    return;
                ws->finishLoad(*batch->frames, batch->failed->load(std::memory_order_relaxed),
                               batch->infeasible->load(std::memory_order_relaxed));
            },
            Qt::QueuedConnection);
    };

    // M47: capability probe on DecodePool; huge sources become metadata placeholders.
    m_comparePaths = paths;
    for (size_t i = 0; i < paths.size(); ++i)
    {
        auto *request = batch->requests[i].get();
        const std::string path = paths[i];
        const int frameIndex = i < frameIndices.size() ? std::max(0, frameIndices[i]) : 0;

        // Promote warm neighbor preload into this batch when available.
        if (frameIndex == 0)
        {
            auto prefetch = takePrefetchHandle(path);
            if (prefetch)
            {
                mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
                bool rejected = false;
                try
                {
                    handle =
                        mviewer::application::ImageLoadingService::instance().promotePreloadAsync(
                            prefetch,
                            [batch, i,
                             finish](const mviewer::application::ImageLoadingService::Result &res)
                            {
                                if (CompareWorkspace::accountLoadRequest(batch, i, &res))
                                    finish();
                            },
                            lifetime);
                }
                catch (...)
                {
                    rejected = true;
                }
                {
                    std::lock_guard<std::mutex> lk(batch->handlesMutex);
                    request->handle = std::move(handle);
                }
                if (rejected || !request->handle)
                {
                    if (accountLoadRequest(batch, i, nullptr, true))
                        finish();
                }
                continue;
            }
        }

        const AccountFn account = [batch, i, finish](bool failed)
        {
            if (CompareWorkspace::accountLoadRequest(batch, i, nullptr, failed))
                finish();
        };
        const ResultFn onResult =
            [batch, i, finish](const mviewer::application::ImageLoadingService::Result &res)
        {
            if (CompareWorkspace::accountLoadRequest(batch, i, &res))
                finish();
        };

        auto probe = TaskScheduler::instance().submit(
            TaskScheduler::Priority::Decode,
            [batch, i, request, path, frameIndex, opts, lifetime, finish, account,
             onResult](const TaskScheduler::TaskContext &ctx)
            {
                runCompareCapabilityProbe(batch, i, request, path, frameIndex, opts, lifetime,
                                          finish, account, onResult, ctx);
            });

        {
            std::lock_guard<std::mutex> lk(batch->handlesMutex);
            request->probeHandle = probe;
        }
        if (!probe)
        {
            if (accountLoadRequest(batch, i, nullptr, true))
                finish();
        }
    }
    cancelPairPrefetch();
}

bool CompareWorkspace::accountLoadRequest(
    const std::shared_ptr<LoadBatch> &batch, size_t index,
    const mviewer::application::ImageLoadingService::Result *result, bool countAsFailure)
{
    if (!batch || index >= batch->requests.size())
        return false;
    auto &request = *batch->requests[index];
    if (request.accounted.exchange(true, std::memory_order_acq_rel))
        return false;

    if (result && result->success() && result->frame)
        (*batch->frames)[index] = result->frame;
    else if (countAsFailure)
        batch->failed->fetch_add(1, std::memory_order_relaxed);

    return batch->remaining->fetch_sub(1, std::memory_order_acq_rel) == 1;
}

void CompareWorkspace::cancelLoadBatch(const std::shared_ptr<LoadBatch> &batch)
{
    if (!batch)
        return;
    for (size_t i = 0; i < batch->requests.size(); ++i)
    {
        mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
        {
            std::lock_guard<std::mutex> lk(batch->handlesMutex);
            TaskScheduler::cancel(batch->requests[i]->probeHandle);
            handle = std::move(batch->requests[i]->handle);
        }
        mviewer::application::ImageLoadingService::instance().cancelAsync(handle);
        accountLoadRequest(batch, i, nullptr);
    }
}
