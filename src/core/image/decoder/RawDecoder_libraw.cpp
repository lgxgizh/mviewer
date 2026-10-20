#include "core/image/decoder/RawDecoder.h"

#include "core/async/CooperativeCancel.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageBuffer.h"
#include "core/image/decoder/RawDecodePlan.h"
#include "core/render/RenderEngine.h"

#include <QFileInfo>

#include <algorithm>
#include <chrono>
#include <cmath>
#include <condition_variable>
#include <cstring>
#include <exception>
#include <memory>
#include <mutex>
#include <thread>

#if defined(MVIEWER_HAS_LIBRAW)
#include "libraw/libraw.h"
#endif

namespace
{
ImageData scaleToEdge(ImageData image, int maxEdge)
{
    if (image.isNull() || maxEdge <= 0)
        return image;
    const int edge = (std::max)(image.width, image.height);
    if (edge <= maxEdge)
        return image;
    const double ratio = static_cast<double>(maxEdge) / static_cast<double>(edge);
    const int tw = (std::max)(1, static_cast<int>(std::lround(image.width * ratio)));
    const int th = (std::max)(1, static_cast<int>(std::lround(image.height * ratio)));
    return RenderEngine::scaleBoundedStatic(image, RenderSize{tw, th});
}

#if defined(MVIEWER_HAS_LIBRAW)
// LibRaw progress callback: a non-zero return aborts the current stage
// (open / unpack / dcraw_process) with LIBRAW_CANCELLED_BY_CALLBACK.
int librawCancelProgress(void *, enum LibRaw_progress, int, int)
{
    return mviewer::core::cooperativeCancelRequested() ? 1 : 0;
}

// The progress callback only runs at stage boundaries; a full AHD demosaic
// spends most of its time inside one stage, polling LibRaw::checkCancel().
// This watcher evaluates the decoding thread's probe chain every 20 ms and
// raises LibRaw's cancel flag, so a stale full decode stops mid-stage.
class LibrawCancelWatch
{
  public:
    explicit LibrawCancelWatch(LibRaw &raw)
    {
        const mviewer::core::ScopedCancelProbe *chain =
            mviewer::core::ScopedCancelProbe::installed();
        if (!chain)
            return;
        try
        {
            m_thread = std::thread(
                [this, &raw, chain]()
                {
                    std::unique_lock<std::mutex> lock(m_mutex);
                    while (!m_cv.wait_for(lock, std::chrono::milliseconds(20),
                                          [this]() { return m_done; }))
                    {
                        if (mviewer::core::ScopedCancelProbe::cancelRequested(chain))
                        {
                            raw.setCancelFlag();
                            return;
                        }
                    }
                });
        }
        catch (...)
        {
            // No watcher thread: stage-boundary cancel still applies.
        }
    }
    ~LibrawCancelWatch()
    {
        if (!m_thread.joinable())
            return;
        {
            std::lock_guard<std::mutex> lock(m_mutex);
            m_done = true;
        }
        m_cv.notify_all();
        m_thread.join();
    }
    LibrawCancelWatch(const LibrawCancelWatch &) = delete;
    LibrawCancelWatch &operator=(const LibrawCancelWatch &) = delete;

  private:
    std::mutex m_mutex;
    std::condition_variable m_cv;
    bool m_done = false;
    std::thread m_thread;
};
#endif
} // namespace

bool RawDecoder::librawAvailable()
{
#if defined(MVIEWER_HAS_LIBRAW)
    return true;
#else
    return false;
#endif
}

ImageData RawDecoder::demosaicWithLibraw(const std::string &path, bool halfSize, int maxEdge) const
{
#if !defined(MVIEWER_HAS_LIBRAW)
    (void)path;
    (void)halfSize;
    (void)maxEdge;
    return {};
#else
    if (mviewer::core::cooperativeCancelRequested())
        return {};
    try
    {
        // Every early return below leaves through ~LibRaw (recycle + free) and
        // the processed image's unique_ptr, including a cancelled stage.
        LibRaw raw;
        raw.set_progress_handler(&librawCancelProgress, nullptr);
        const auto native = mviewer::core::pathFromUtf8(path);
#if defined(_WIN32)
        const int opened = raw.open_file(native.wstring().c_str());
#else
        const int opened = raw.open_file(native.string().c_str());
#endif
        if (opened != LIBRAW_SUCCESS)
            return {};

        const int sensorW = (std::max)(raw.imgdata.sizes.raw_width, raw.imgdata.sizes.width);
        const int sensorH = (std::max)(raw.imgdata.sizes.raw_height, raw.imgdata.sizes.height);
        const int sensorEdge = (std::max)(sensorW, sensorH);
        if (halfSize && !mviewer::core::rawHalfCoversRequest(sensorEdge, maxEdge))
        {
            raw.recycle();
            return {};
        }

        raw.imgdata.params.half_size = halfSize ? 1 : 0;
        raw.imgdata.params.use_camera_wb = 1;
        raw.imgdata.params.output_bps = 8;
        raw.imgdata.params.no_auto_bright = 1;
        raw.imgdata.params.output_color = 1; // sRGB
        raw.imgdata.params.user_qual = halfSize ? 0 : 3;
        if (raw.unpack() != LIBRAW_SUCCESS)
        {
            raw.recycle();
            return {};
        }
        // A stale request aborts the demosaic from the progress callback (or
        // the watcher, mid-stage); decodeRawPixels() then reports it as
        // cancelled, not as a broken file.
        int processed = LIBRAW_UNSPECIFIED_ERROR;
        {
            const std::unique_ptr<LibrawCancelWatch> watch =
                halfSize ? nullptr : std::make_unique<LibrawCancelWatch>(raw);
            processed = raw.dcraw_process();
        }
        if (processed != LIBRAW_SUCCESS || mviewer::core::cooperativeCancelRequested())
        {
            raw.recycle();
            return {};
        }
        struct MemImageDeleter
        {
            void operator()(libraw_processed_image_t *p) const noexcept
            {
                if (p)
                    LibRaw::dcraw_clear_mem(p);
            }
        };
        int err = 0;
        std::unique_ptr<libraw_processed_image_t, MemImageDeleter> image(
            raw.dcraw_make_mem_image(&err));
        if (!image || err != LIBRAW_SUCCESS || image->bits != 8 || image->width <= 0 ||
            image->height <= 0 || image->colors < 1)
        {
            raw.recycle();
            return {};
        }

        const int channels = image->colors >= 3 ? 3 : 1;
        const PixelFormat format = channels == 1 ? PixelFormat::Grayscale8 : PixelFormat::RGB24;
        ImageData out =
            makeImageData(static_cast<int>(image->width), static_cast<int>(image->height), format);
        const size_t srcChannels = static_cast<size_t>(image->colors < 1 ? 1 : image->colors);
        auto *dst = out.buffer->data();
        const auto *src = image->data;
        const size_t pixels =
            static_cast<size_t>(image->width) * static_cast<size_t>(image->height);
        const size_t need = pixels * srcChannels;
        if (image->data_size < need)
        {
            raw.recycle();
            return {};
        }
        if (srcChannels == static_cast<size_t>(channels))
        {
            std::memcpy(dst, src, out.byteSize());
        }
        else
        {
            for (size_t i = 0; i < pixels; ++i)
            {
                dst[i * 3] = src[i * srcChannels];
                dst[i * 3 + 1] = src[i * srcChannels + 1];
                dst[i * 3 + 2] = src[i * srcChannels + 2];
            }
        }
        image.reset();
        raw.recycle();
        return scaleToEdge(std::move(out), maxEdge);
    }
    catch (...)
    {
        return {};
    }
#endif
}

ImageData RawDecoder::decodeRawPixels(const std::string &path, int maxEdge, bool allowFull) const
{
    // A stale request does no work and never falls back to the preview: the
    // caller is gone, and an empty result plus cooperativeCancelRequested()
    // is reported as DecodeErrorCode::Cancelled.
    if (mviewer::core::cooperativeCancelRequested())
        return {};
    // One preview decode. When it already covers the request (or LibRaw is not
    // linked) this is the whole path — the historical embedded-JPEG fast path.
    const int previewEdgeRequest = maxEdge > 0 ? maxEdge : 0;
    ImageData preview = extractPreview(path, previewEdgeRequest);
    const int previewEdge = preview.isNull() ? 0 : (std::max)(preview.width, preview.height);
    if (!librawAvailable())
        return preview;

    if (!allowFull)
    {
        const auto intent = mviewer::core::chooseRawLod(previewEdge, maxEdge, true);
        if (intent != mviewer::core::RawLodIntent::TryHalf)
            return preview;
        ImageData half = demosaicWithLibraw(path, true, maxEdge);
        if (!half.isNull())
            return half;
        if (mviewer::core::cooperativeCancelRequested())
            return {};
        return preview;
    }

    const QFileInfo info(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    const auto full = mviewer::core::chooseRawFull(previewEdge, true, info.size());
    if (full == mviewer::core::RawFullIntent::FullDemosaic)
    {
        ImageData demosaic = demosaicWithLibraw(path, false, 0);
        if (!demosaic.isNull())
            return demosaic;
        if (mviewer::core::cooperativeCancelRequested())
            return {};
    }
    return preview;
}
