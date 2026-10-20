#include "application/Shutdown.h"

#include "core/Logger.h"
#include "core/MainThreadDispatcher.h"
#include "core/RatingStore.h"
#include "core/TagStore.h"
#include "core/image/FrameSequence.h"
#include "core/metadata/MetadataIndexer.h"
#include "core/render/DisplayResample.h"
#include "core/scheduler/TaskScheduler.h"
#include "core/thumbnail/ThumbnailPipeline.h"

#include <QCoreApplication>
#include <QEvent>
#include <QEventLoop>
#include <QThreadPool>

#include <mutex>
#include <utility>
#include <vector>

namespace
{

std::mutex g_hooksMutex;
std::vector<std::function<void()>> g_hooks;
bool g_shutdownStarted = false;

void drainPostedEvents()
{
    QCoreApplication *app = QCoreApplication::instance();
    if (!app)
        return;
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    app->processEvents(QEventLoop::AllEvents, 100);
    QCoreApplication::sendPostedEvents(nullptr, QEvent::DeferredDelete);
    app->processEvents(QEventLoop::AllEvents, 50);
}

} // namespace

namespace mviewer::application
{

void registerProcessShutdownHook(std::function<void()> hook)
{
    if (!hook)
        return;
    std::function<void()> pending;
    {
        std::lock_guard<std::mutex> lock(g_hooksMutex);
        if (g_shutdownStarted)
            pending = std::move(hook);
        else
            g_hooks.push_back(std::move(hook));
    }
    if (pending)
        pending();
}

void shutdownProcessServices()
{
    std::vector<std::function<void()>> hooks;
    {
        std::lock_guard<std::mutex> lock(g_hooksMutex);
        if (g_shutdownStarted)
            return;
        g_shutdownStarted = true;
        hooks.swap(g_hooks);
    }

    drainPostedEvents();

    for (auto &hook : hooks)
    {
        if (hook)
            hook();
    }

    mviewer::core::MetadataIndexer::instance().cancel();
    ThumbnailPipeline::instance().clear();
    mviewer::core::FrameSequenceReader::releaseFile({});
    mviewer::core::TagStore::joinBackgroundWorker();
    mviewer::core::RatingStore::joinBackgroundWorker();

    TaskScheduler::instance().shutdown();
    mviewer::core::waitForDisplayResampleIdle();
    if (QThreadPool *globalPool = QThreadPool::globalInstance())
        globalPool->waitForDone();

    mvcore::setMainThreadDispatcher({});
    mviewer::core::shutdownFileLogger();

    drainPostedEvents();
}

ScopedProcessShutdown::ScopedProcessShutdown() = default;

ScopedProcessShutdown::~ScopedProcessShutdown()
{
    shutdownProcessServices();
}

} // namespace mviewer::application
