// M53 bounded TIFF display raster through the Windows Imaging Component.
// Split from QtDecoder.cpp (complexity gate): the WIC pipeline is built in
// small steps (open frame -> clip -> Fant scale -> BGR convert -> copy) and
// only the requested display raster is materialized.
#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <string>

#if defined(Q_OS_WIN)
#define NOMINMAX
#include <wincodec.h>
#include <windows.h>
#include <wrl/client.h>

#include <algorithm>
#include <cstdint>
#include <limits>
#include <vector>
#endif

namespace mviewer::core::detail
{
QImage decodeTiffWicRaster(const std::string &path, const QRect &rawRect, const QSize &target,
                           QSize &fullSize);
} // namespace mviewer::core::detail

#if defined(Q_OS_WIN)

namespace
{

using Microsoft::WRL::ComPtr;

// COM apartment guard for the WIC path. RPC_E_CHANGED_MODE means the thread
// already joined an STA, which WIC also accepts.
class ComScope
{
  public:
    ComScope() : result_(CoInitializeEx(nullptr, COINIT_MULTITHREADED))
    {
    }
    ~ComScope()
    {
        if (SUCCEEDED(result_))
            CoUninitialize();
    }
    ComScope(const ComScope &) = delete;
    ComScope &operator=(const ComScope &) = delete;
    bool usable() const
    {
        return SUCCEEDED(result_) || result_ == RPC_E_CHANGED_MODE;
    }

  private:
    HRESULT result_;
};

// Every WIC object of one decode. Members are declared in pipeline order so
// they are released in reverse order (converter first, factory last), all
// before the enclosing ComScope uninitializes COM.
struct WicTiffPipeline
{
    ComPtr<IWICImagingFactory> factory;
    ComPtr<IWICBitmapDecoder> decoder;
    ComPtr<IWICBitmapFrameDecode> frame;
    ComPtr<IWICBitmapSource> source;
    ComPtr<IWICBitmapScaler> scaler;
    ComPtr<IWICFormatConverter> converter;
    UINT fullWidth = 0;
    UINT fullHeight = 0;
};

bool isValidWicRequest(const QRect &rawRect, const QSize &target)
{
    return target.width() > 0 && target.height() > 0 && rawRect.width() > 0 && rawRect.height() > 0;
}

// Opens frame 0 and records the full source size (bounded to int range).
bool openWicTiffFrame(const std::wstring &filenameWide, WicTiffPipeline &wic)
{
    if (FAILED(CoCreateInstance(CLSID_WICImagingFactory, nullptr, CLSCTX_INPROC_SERVER,
                                IID_PPV_ARGS(&wic.factory))))
        return false;
    if (FAILED(wic.factory->CreateDecoderFromFilename(filenameWide.c_str(), nullptr, GENERIC_READ,
                                                      WICDecodeMetadataCacheOnDemand,
                                                      &wic.decoder)))
        return false;
    if (FAILED(wic.decoder->GetFrame(0, &wic.frame)))
        return false;
    if (FAILED(wic.frame->GetSize(&wic.fullWidth, &wic.fullHeight)))
        return false;
    constexpr UINT kMaxDim = static_cast<UINT>(std::numeric_limits<int>::max());
    return wic.fullWidth != 0 && wic.fullHeight != 0 && wic.fullWidth <= kMaxDim &&
           wic.fullHeight <= kMaxDim;
}

// Intersects the requested raw rect with the frame; an empty QRect means the
// request lies outside the source.
QRect clampWicClip(const QRect &rawRect, UINT fullWidth, UINT fullHeight)
{
    const int x0 = std::max(0, rawRect.x());
    const int y0 = std::max(0, rawRect.y());
    const int x1 = std::min(static_cast<int>(fullWidth), rawRect.x() + rawRect.width());
    const int y1 = std::min(static_cast<int>(fullHeight), rawRect.y() + rawRect.height());
    if (x1 <= x0 || y1 <= y0)
        return QRect();
    return QRect(x0, y0, x1 - x0, y1 - y0);
}

unsigned maskBitCount(const BYTE *mask, UINT size)
{
    unsigned bits = 0;
    for (UINT i = 0; i < size; ++i)
    {
        unsigned char v = mask[i];
        while (v != 0)
        {
            bits += v & 1u;
            v = static_cast<unsigned char>(v >> 1u);
        }
    }
    return bits;
}

// Widest channel in the pixel format, from the channel masks. 0 means the
// query did not produce a usable answer (caller keeps undithered conversion).
UINT widestChannelBits(IWICPixelFormatInfo *info)
{
    if (info == nullptr)
        return 0;
    UINT channels = 0;
    UINT bitsPerPixel = 0;
    if (FAILED(info->GetChannelCount(&channels)) || channels == 0)
        return 0;
    if (FAILED(info->GetBitsPerPixel(&bitsPerPixel)))
        bitsPerPixel = 0;
    if (channels > 16)
        channels = 16;
    unsigned widest = 0;
    UINT measured = 0;
    for (UINT c = 0; c < channels; ++c)
    {
        BYTE mask[8] = {};
        UINT actual = 0;
        if (FAILED(info->GetChannelMask(c, static_cast<UINT>(sizeof(mask)), mask, &actual)))
            continue;
        if (actual == 0 || actual > sizeof(mask))
            continue;
        const unsigned bits = maskBitCount(mask, actual);
        ++measured;
        if (bits > widest)
            widest = bits;
    }
    if (measured > 0 && widest > 0)
        return widest;
    if (bitsPerPixel == 0)
        return 0;
    return bitsPerPixel / channels;
}

// Bits per channel of `source`, or 0 when the format cannot be queried.
// A 0 result must not fail the decode; the converter stays undithered.
UINT sourceBitsPerChannel(IWICImagingFactory *factory, IWICBitmapSource *source)
{
    if (factory == nullptr || source == nullptr)
        return 0;
    WICPixelFormatGUID pixelFormat{};
    if (FAILED(source->GetPixelFormat(&pixelFormat)))
        return 0;
    ComPtr<IWICComponentInfo> component;
    if (FAILED(factory->CreateComponentInfo(pixelFormat, &component)))
        return 0;
    ComPtr<IWICPixelFormatInfo> info;
    if (FAILED(component.As(&info)) || !info)
        return 0;
    return widestChannelBits(info.Get());
}

bool initBgrConverter(WicTiffPipeline &wic, WICBitmapDitherType dither)
{
    if (FAILED(wic.factory->CreateFormatConverter(&wic.converter)))
        return false;
    return SUCCEEDED(wic.converter->Initialize(wic.scaler.Get(), GUID_WICPixelFormat24bppBGR,
                                               dither, nullptr, 0.0, WICBitmapPaletteTypeCustom));
}

// Builds frame -> (clipper) -> Fant scaler -> 24bpp BGR converter. Nothing is
// decoded yet; WIC pulls only the clipped, scaled raster on CopyPixels.
// Sources with more than 8 bits per channel request error-diffusion dither.
// 8-bit sources, and any failed format query, stay on WICBitmapDitherTypeNone.
bool buildWicDisplaySource(WicTiffPipeline &wic, const QRect &clip, const QSize &target)
{
    const QRect full(0, 0, static_cast<int>(wic.fullWidth), static_cast<int>(wic.fullHeight));
    if (clip == full)
    {
        wic.source = wic.frame;
    }
    else
    {
        ComPtr<IWICBitmapClipper> clipper;
        const WICRect clipRect{clip.x(), clip.y(), clip.width(), clip.height()};
        if (FAILED(wic.factory->CreateBitmapClipper(&clipper)) ||
            FAILED(clipper->Initialize(wic.frame.Get(), &clipRect)))
            return false;
        wic.source = clipper;
    }
    if (FAILED(wic.factory->CreateBitmapScaler(&wic.scaler)) ||
        FAILED(wic.scaler->Initialize(wic.source.Get(), static_cast<UINT>(target.width()),
                                      static_cast<UINT>(target.height()),
                                      WICBitmapInterpolationModeFant)))
        return false;
    UINT bitsPerChannel = sourceBitsPerChannel(wic.factory.Get(), wic.source.Get());
    if (bitsPerChannel == 0 && wic.source.Get() != wic.frame.Get())
        bitsPerChannel = sourceBitsPerChannel(wic.factory.Get(), wic.frame.Get());
    const WICBitmapDitherType dither =
        bitsPerChannel > 8 ? WICBitmapDitherTypeErrorDiffusion : WICBitmapDitherTypeNone;
    if (initBgrConverter(wic, dither))
        return true;
    // Some WIC builds reject dither into a non-indexed destination. Retry
    // undithered so a 16-bit frame still decodes.
    if (dither == WICBitmapDitherTypeNone)
        return false;
    return initBgrConverter(wic, WICBitmapDitherTypeNone);
}

// Materializes the converter output as an RGB888 QImage (null on failure).
QImage copyWicBgrToRgb(IWICFormatConverter *converter, const QSize &target)
{
    const qint64 outputBytes = static_cast<qint64>(target.width()) * target.height() * 3;
    if (outputBytes <= 0 || outputBytes > std::numeric_limits<UINT>::max())
        return QImage();
    QImage output(target, QImage::Format_RGB888);
    if (output.isNull())
        return QImage();
    const UINT stride = static_cast<UINT>(target.width() * 3);
    const UINT byteCount = stride * static_cast<UINT>(target.height());
    std::vector<uint8_t> bgr(byteCount);
    if (FAILED(converter->CopyPixels(nullptr, stride, byteCount, bgr.data())))
        return QImage();
    for (int y = 0; y < target.height(); ++y)
    {
        const uint8_t *src = bgr.data() + static_cast<size_t>(y) * stride;
        uchar *dst = output.scanLine(y);
        for (int x = 0; x < target.width(); ++x)
        {
            dst[x * 3 + 0] = src[x * 3 + 2];
            dst[x * 3 + 1] = src[x * 3 + 1];
            dst[x * 3 + 2] = src[x * 3 + 0];
        }
    }
    return output;
}

} // namespace

namespace mviewer::core::detail
{
QImage decodeTiffWicRaster(const std::string &path, const QRect &rawRect, const QSize &target,
                           QSize &fullSize)
{
    if (!isValidWicRequest(rawRect, target))
        return QImage();
    const QString filename = QString::fromUtf8(path.data(), static_cast<int>(path.size()));
    const ComScope com;
    if (!com.usable())
        return QImage();
    WicTiffPipeline wic;
    if (!openWicTiffFrame(filename.toStdWString(), wic))
        return QImage();
    const QRect clip = clampWicClip(rawRect, wic.fullWidth, wic.fullHeight);
    if (clip.isEmpty() || !buildWicDisplaySource(wic, clip, target))
        return QImage();
    QImage output = copyWicBgrToRgb(wic.converter.Get(), target);
    if (!output.isNull())
        fullSize = QSize(static_cast<int>(wic.fullWidth), static_cast<int>(wic.fullHeight));
    return output;
}
} // namespace mviewer::core::detail

#else

namespace mviewer::core::detail
{
QImage decodeTiffWicRaster(const std::string &, const QRect &, const QSize &, QSize &)
{
    return QImage();
}
} // namespace mviewer::core::detail

#endif
