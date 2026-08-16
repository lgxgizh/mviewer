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

    AsyncRequestHandle loadAsyncCancellable(
        const std::string &path, std::function<void(const Result &)> callback,
        const ImageLoadOptions &options = ImageRepository::kDefaultLoadOptions)
    {
        return mviewer::core::ImageLoadingFacade::instance().loadAsyncCancellable(
            path, std::move(callback), options);
    }

    AsyncRequestHandle preloadAsync(const std::string &path)
    {
        return mviewer::core::ImageLoadingFacade::instance().preloadAsync(path);
    }

    AsyncRequestHandle promotePreloadAsync(
        AsyncRequestHandle &preload, std::function<void(const Result &)> callback)
    {
        return mviewer::core::ImageLoadingFacade::instance().promotePreloadAsync(
            preload, std::move(callback));
    }

    void cancelAsync(AsyncRequestHandle &handle)
    {
        mviewer::core::ImageLoadingFacade::instance().cancelAsync(handle);
    }

    std::string makeKey(const std::string &path) const
    {
        return mviewer::core::ImageLoadingFacade::instance().makeKey(path);
    }

  private:
    ImageLoadingService() = default;
};

} // namespace mviewer::application
