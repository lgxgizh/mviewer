#pragma once

#include "core/image/ImageLoadingFacade.h"

#include <functional>
#include <string>
#include <utility>

namespace mviewer::application
{

// Thin Application-layer facade. Widgets depend on this contract rather than
// naming ImageRepository directly; all loading remains cancellable and
// latest-request ownership stays with the caller.
class ImageLoadingService
{
  public:
    using Result = mviewer::core::ImageLoadResult;
    using AsyncRequestHandle = mviewer::core::ImageAsyncRequestHandle;

    static ImageLoadingService &instance()
    {
        static ImageLoadingService service;
        return service;
    }

    // M46: `lifetime` is the consumer's AsyncLifetimeToken (see
    // ImageLoadingFacade). Widgets create one token per QObject and invalidate
    // it in the destructor; expired tokens suppress late client deliveries.
    AsyncRequestHandle loadAsyncCancellable(
        const std::string &path, std::function<void(const Result &)> callback,
        const ImageLoadOptions &options = ImageRepository::kDefaultLoadOptions,
        std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime = {})
    {
        return mviewer::core::ImageLoadingFacade::instance().loadAsyncCancellable(
            path, std::move(callback), options, std::move(lifetime));
    }

    AsyncRequestHandle loadFrameAsync(
        const std::string &path, int frameIndex, std::function<void(const Result &)> callback,
        const ImageLoadOptions &options = ImageRepository::kDefaultLoadOptions,
        std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime = {})
    {
        return mviewer::core::ImageLoadingFacade::instance().loadFrameAsync(
            path, frameIndex, std::move(callback), options, std::move(lifetime));
    }

    AsyncRequestHandle preloadAsync(
        const std::string &path,
        std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime = {})
    {
        return mviewer::core::ImageLoadingFacade::instance().preloadAsync(
            path, std::move(lifetime));
    }

    AsyncRequestHandle promotePreloadAsync(
        AsyncRequestHandle &preload, std::function<void(const Result &)> callback,
        std::weak_ptr<mviewer::core::AsyncLifetimeToken> lifetime = {})
    {
        return mviewer::core::ImageLoadingFacade::instance().promotePreloadAsync(
            preload, std::move(callback), std::move(lifetime));
    }

    void cancelAsync(AsyncRequestHandle &handle)
    {
        mviewer::core::ImageLoadingFacade::instance().cancelAsync(handle);
    }

    std::string makeKey(const std::string &path) const
    {
        return mviewer::core::ImageLoadingFacade::instance().makeKey(path);
    }

    bool getPreviewCache(const std::string &key, ImageData &out) const
    {
        return mviewer::core::ImageLoadingFacade::instance().getPreviewCache(key, out);
    }

    void putPreviewCache(const std::string &key, const ImageData &image)
    {
        mviewer::core::ImageLoadingFacade::instance().putPreviewCache(key, image);
    }

    void invalidateSource(const std::string &path)
    {
        mviewer::core::ImageLoadingFacade::instance().invalidateSource(path);
    }

  private:
    ImageLoadingService() = default;
};

} // namespace mviewer::application
