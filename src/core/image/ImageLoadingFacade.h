#pragma once

#include "core/image/ImageRepository.h"

#include <functional>
#include <string>
#include <utility>

// Core-owned loading boundary used by the Application/UI layers.  The
// repository remains the implementation and keeps its cache/decoder details
// private from widgets; this header only exposes the value/request contracts.
namespace mviewer::core
{

using ImageLoadResult = ::ImageRepository::Result;
using ImageAsyncRequestHandle = ::ImageRepository::AsyncRequestHandle;

class ImageLoadingFacade
{
  public:
    static ImageLoadingFacade &instance()
    {
        static ImageLoadingFacade facade;
        return facade;
    }

    ImageAsyncRequestHandle loadAsyncCancellable(
        const std::string &path, std::function<void(const ImageLoadResult &)> callback,
        const ImageLoadOptions &options = ImageRepository::kDefaultLoadOptions)
    {
        return ImageRepository::instance().loadAsyncCancellable(path, std::move(callback), options);
    }

    ImageAsyncRequestHandle preloadAsync(const std::string &path)
    {
        return ImageRepository::instance().preloadAsync(path);
    }

    ImageAsyncRequestHandle promotePreloadAsync(
        ImageAsyncRequestHandle &preload,
        std::function<void(const ImageLoadResult &)> callback)
    {
        return ImageRepository::instance().promotePreloadAsync(preload, std::move(callback));
    }

    void cancelAsync(ImageAsyncRequestHandle &handle)
    {
        ImageRepository::instance().cancelAsync(handle);
    }

    std::string makeKey(const std::string &path) const
    {
        return ImageRepository::instance().makeKey(path);
    }

  private:
    ImageLoadingFacade() = default;
};

} // namespace mviewer::core
