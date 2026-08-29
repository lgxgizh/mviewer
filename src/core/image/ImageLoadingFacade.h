#pragma once

#include "core/async/AsyncLifetimeToken.h"
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

    // M46: `lifetime` is the consumer's AsyncLifetimeToken. The request layer
    // holds a weak_ptr; once the consumer invalidates/expires the token, any
    // late completion is suppressed BEFORE the client callback starts.
    ImageAsyncRequestHandle loadAsyncCancellable(
        const std::string &path, std::function<void(const ImageLoadResult &)> callback,
        const ImageLoadOptions &options = ImageRepository::kDefaultLoadOptions,
        std::weak_ptr<AsyncLifetimeToken> lifetime = {})
    {
        return ImageRepository::instance().loadAsyncCancellable(
            path, std::move(callback), options, std::move(lifetime));
    }

    ImageAsyncRequestHandle preloadAsync(
        const std::string &path, std::weak_ptr<AsyncLifetimeToken> lifetime = {})
    {
        return ImageRepository::instance().preloadAsync(path, std::move(lifetime));
    }

    ImageAsyncRequestHandle promotePreloadAsync(
        ImageAsyncRequestHandle &preload, std::function<void(const ImageLoadResult &)> callback,
        std::weak_ptr<AsyncLifetimeToken> lifetime = {})
    {
        return ImageRepository::instance().promotePreloadAsync(preload, std::move(callback),
                                                               std::move(lifetime));
    }

    void cancelAsync(ImageAsyncRequestHandle &handle)
    {
        ImageRepository::instance().cancelAsync(handle);
    }

    std::string makeKey(const std::string &path) const
    {
        return ImageRepository::instance().makeKey(path);
    }

    // UI preview boundary: cache ownership stays inside the repository/cache
    // stack, while the PreviewPanel only sees a value-owned ImageData payload.
    bool getPreviewCache(const std::string &key, ImageData &out) const
    {
        return ImageRepository::instance().getPreviewCache(key, out);
    }

    void putPreviewCache(const std::string &key, const ImageData &image)
    {
        ImageRepository::instance().putPreviewCache(key, image);
    }

    void invalidateSource(const std::string &path)
    {
        ImageRepository::instance().invalidate(path);
    }

  private:
    ImageLoadingFacade() = default;
};

} // namespace mviewer::core
