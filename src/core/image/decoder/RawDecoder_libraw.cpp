#include "core/image/decoder/RawDecoder.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageBuffer.h"
#include "core/image/decoder/RawDecodePlan.h"
#include "core/render/RenderEngine.h"

#include <QFileInfo>

#include <algorithm>
#include <cmath>
#include <cstring>
#include <exception>

#if defined(MVIEWER_HAS_LIBRAW)
#include "libraw/libraw.h"
#endif

namespace
{
ImageData scaleToEdge(ImageData image, int maxEdge)
{
    if (image.isNull() || maxEdge <= 0)
        return image;
    const int edge = std::max(image.width, image.height);
    if (edge <= maxEdge)
        return image;
    const double ratio = static_cast<double>(maxEdge) / static_cast<double>(edge);
    const int tw = std::max(1, static_cast<int>(std::lround(image.width * ratio)));
    const int th = std::max(1, static_cast<int>(std::lround(image.height * ratio)));
    return RenderEngine::scaleBoundedStatic(image, RenderSize{tw, th});
}
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
    try
    {
        LibRaw raw;
        const auto native = mviewer::core::pathFromUtf8(path);
#if defined(_WIN32)
        const int opened = raw.open_file(native.wstring().c_str());
#else
        const int opened = raw.open_file(native.string().c_str());
#endif
        if (opened != LIBRAW_SUCCESS)
            return {};

        const int sensorW = std::max(raw.imgdata.sizes.raw_width, raw.imgdata.sizes.width);
        const int sensorH = std::max(raw.imgdata.sizes.raw_height, raw.imgdata.sizes.height);
        const int sensorEdge = std::max(sensorW, sensorH);
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
        if (raw.dcraw_process() != LIBRAW_SUCCESS)
        {
            raw.recycle();
            return {};
        }
        int err = 0;
        libraw_processed_image_t *image = raw.dcraw_make_mem_image(&err);
        if (!image || err != LIBRAW_SUCCESS || image->bits != 8 || image->width <= 0 ||
            image->height <= 0 || image->colors < 1)
        {
            if (image)
                LibRaw::dcraw_clear_mem(image);
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
        const int width = static_cast<int>(image->width);
        const int height = static_cast<int>(image->height);
        if (srcChannels == static_cast<size_t>(channels))
        {
            std::memcpy(dst, src, out.byteSize());
        }
        else
        {
            for (int i = 0; i < width * height; ++i)
            {
                dst[static_cast<size_t>(i) * 3] = src[static_cast<size_t>(i) * srcChannels];
                dst[static_cast<size_t>(i) * 3 + 1] = src[static_cast<size_t>(i) * srcChannels + 1];
                dst[static_cast<size_t>(i) * 3 + 2] = src[static_cast<size_t>(i) * srcChannels + 2];
            }
        }
        LibRaw::dcraw_clear_mem(image);
        raw.recycle();
        return scaleToEdge(std::move(out), maxEdge);
    }
    catch (const std::exception &)
    {
        return {};
    }
#endif
}

ImageData RawDecoder::decodeRawPixels(const std::string &path, int maxEdge, bool allowFull) const
{
    // One preview decode. When it already covers the request (or LibRaw is not
    // linked) this is the whole path — the historical embedded-JPEG fast path.
    const int previewEdgeRequest = maxEdge > 0 ? maxEdge : 0;
    ImageData preview = extractPreview(path, previewEdgeRequest);
    const int previewEdge = preview.isNull() ? 0 : std::max(preview.width, preview.height);
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
        return preview;
    }

    const QFileInfo info(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    const auto full = mviewer::core::chooseRawFull(previewEdge, true, info.size());
    if (full == mviewer::core::RawFullIntent::FullDemosaic)
    {
        ImageData demosaic = demosaicWithLibraw(path, false, 0);
        if (!demosaic.isNull())
            return demosaic;
    }
    return preview;
}
