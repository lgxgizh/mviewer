#include "core/image/QtConvert.h"

#include <cstring>

namespace mvcore
{

QImage toQImage(const ImageData &src)
{
    if (src.isNull())
        return QImage();
    const ImageBuffer v = src.view();
    switch (src.format)
    {
    case PixelFormat::Grayscale8:
    {
        QImage out(v.width, v.height, QImage::Format_Grayscale8);
        if (out.isNull())
            return QImage();
        const size_t rowBytes = static_cast<size_t>(v.width);
        for (int y = 0; y < v.height; ++y)
        {
            std::memcpy(out.scanLine(y), v.data + static_cast<size_t>(y) * v.stride(), rowBytes);
        }
        return out;
    }
    case PixelFormat::RGBA32:
    case PixelFormat::BGRA32:
    {
        QImage out(v.width, v.height, QImage::Format_ARGB32);
        if (out.isNull())
            return QImage();
        const int cpp = v.channelsPerPixel();
        for (int y = 0; y < v.height; ++y)
        {
            const uint8_t *sl = v.data + static_cast<size_t>(y) * v.stride();
            QRgb *dl = reinterpret_cast<QRgb *>(out.scanLine(y));
            for (int x = 0; x < v.width; ++x)
            {
                const uint8_t *p = sl + x * cpp;
                if (src.format == PixelFormat::RGBA32)
                    dl[x] = qRgba(p[0], p[1], p[2], p[3]);
                else // BGRA32
                    dl[x] = qRgba(p[2], p[1], p[0], p[3]);
            }
        }
        return out;
    }
    case PixelFormat::RGB24:
    case PixelFormat::BGR24:
    default:
    {
        QImage out(v.width, v.height, QImage::Format_RGB888);
        if (out.isNull())
            return QImage();
        const int cpp = v.channelsPerPixel();
        for (int y = 0; y < v.height; ++y)
        {
            const uint8_t *sl = v.data + static_cast<size_t>(y) * v.stride();
            uchar *dl = out.scanLine(y);
            for (int x = 0; x < v.width; ++x)
            {
                const uint8_t *p = sl + x * cpp;
                if (src.format == PixelFormat::BGR24)
                {
                    dl[x * 3 + 0] = p[2];
                    dl[x * 3 + 1] = p[1];
                    dl[x * 3 + 2] = p[0];
                }
                else
                {
                    dl[x * 3 + 0] = p[0];
                    dl[x * 3 + 1] = p[1];
                    dl[x * 3 + 2] = p[2];
                }
            }
        }
        return out;
    }
    }
}

ImageData fromQImage(const QImage &src)
{
    if (src.isNull())
        return ImageData();

    if (src.format() == QImage::Format_Grayscale8)
    {
        ImageData out = makeImageData(src.width(), src.height(), PixelFormat::Grayscale8);
        const size_t rowBytes = static_cast<size_t>(src.width());
        for (int y = 0; y < src.height(); ++y)
        {
            std::memcpy(out.buffer.get() + static_cast<size_t>(y) * out.stride(),
                        src.constScanLine(y), rowBytes);
        }
        return out;
    }

    const QImage img = src.convertToFormat(QImage::Format_RGB888);
    if (img.isNull())
        return ImageData();
    ImageData out = makeImageData(img.width(), img.height(), PixelFormat::RGB24);
    const size_t rowBytes = static_cast<size_t>(img.width()) * 3;
    for (int y = 0; y < img.height(); ++y)
    {
        std::memcpy(out.buffer.get() + static_cast<size_t>(y) * out.stride(), img.constScanLine(y),
                    rowBytes);
    }
    return out;
}

} // namespace mvcore
