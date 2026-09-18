#include "core/image/QtConvert.h"
#include "core/simd/CpuFeatures.h"

#include <QByteArray>
#include <QColorSpace>
#include <algorithm>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
#define MV_TARGET_AVX2 __attribute__((target("avx2")))
#define MV_TARGET_SSSE3 __attribute__((target("ssse3,sse4.1")))
#else
#define MV_TARGET_AVX2
#define MV_TARGET_SSSE3
#endif

namespace
{

MV_TARGET_AVX2
inline void convertRgba32ToArgb32RowAVX2(const uint8_t *src, uint8_t *dst, int width)
{
    const __m256i mask = _mm256_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15, 2,
                                          1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
    size_t x = 0;
    const size_t w = static_cast<size_t>(width);
    for (; x + 8 <= w; x += 8)
    {
        const size_t offset = x * 4;
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + offset));
        const __m256i shuffled = _mm256_shuffle_epi8(v, mask);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + offset), shuffled);
    }
    for (; x < w; ++x)
    {
        const size_t offset = x * 4;
        const uint8_t *p = src + offset;
        uint8_t *d = dst + offset;
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        d[3] = p[3];
    }
}

MV_TARGET_SSSE3
inline void convertRgba32ToArgb32RowSSSE3(const uint8_t *src, uint8_t *dst, int width)
{
    const __m128i mask = _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);
    size_t x = 0;
    const size_t w = static_cast<size_t>(width);
    for (; x + 4 <= w; x += 4)
    {
        const size_t offset = x * 4;
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + offset));
        const __m128i shuffled = _mm_shuffle_epi8(v, mask);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + offset), shuffled);
    }
    for (; x < w; ++x)
    {
        const size_t offset = x * 4;
        const uint8_t *p = src + offset;
        uint8_t *d = dst + offset;
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        d[3] = p[3];
    }
}

inline void convertRgba32ToArgb32RowScalar(const uint8_t *src, uint8_t *dst, int width)
{
    size_t x = 0;
    const size_t w = static_cast<size_t>(width);
    for (; x + 4 <= w; x += 4)
    {
        const size_t offset = x * 4;
        const uint8_t *p = src + offset;
        uint8_t *d = dst + offset;
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        d[3] = p[3];
        d[4] = p[6];
        d[5] = p[5];
        d[6] = p[4];
        d[7] = p[7];
        d[8] = p[10];
        d[9] = p[9];
        d[10] = p[8];
        d[11] = p[11];
        d[12] = p[14];
        d[13] = p[13];
        d[14] = p[12];
        d[15] = p[15];
    }
    for (; x < w; ++x)
    {
        const size_t offset = x * 4;
        const uint8_t *p = src + offset;
        uint8_t *d = dst + offset;
        d[0] = p[2];
        d[1] = p[1];
        d[2] = p[0];
        d[3] = p[3];
    }
}

inline void convertBgr24ToRgb888Row(const uint8_t *src, uint8_t *dst, int width)
{
    size_t x = 0;
    const size_t w = static_cast<size_t>(width);
    for (; x + 4 <= w; x += 4)
    {
        const size_t sOff = x * 3;
        const uint8_t *s = src + sOff;
        uint8_t *d = dst + sOff;
        d[0] = s[2];
        d[1] = s[1];
        d[2] = s[0];
        d[3] = s[5];
        d[4] = s[4];
        d[5] = s[3];
        d[6] = s[8];
        d[7] = s[7];
        d[8] = s[6];
        d[9] = s[11];
        d[10] = s[10];
        d[11] = s[9];
    }
    for (; x < w; ++x)
    {
        const size_t sOff = x * 3;
        const uint8_t *s = src + sOff;
        uint8_t *d = dst + sOff;
        d[0] = s[2];
        d[1] = s[1];
        d[2] = s[0];
    }
}

} // namespace

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
        const size_t outStride = static_cast<size_t>(out.bytesPerLine());
        const size_t vStride = static_cast<size_t>(v.stride());
        if (vStride == outStride && vStride == rowBytes)
        {
            std::memcpy(out.bits(), v.data, static_cast<size_t>(v.height) * rowBytes);
        }
        else
        {
            for (int y = 0; y < v.height; ++y)
            {
                std::memcpy(out.scanLine(y), v.data + static_cast<size_t>(y) * vStride, rowBytes);
            }
        }
        return out;
    }
    case PixelFormat::BGRA32:
    {
        // On little-endian systems, BGRA32 byte layout is identical to Qt's Format_ARGB32 (B, G, R,
        // A)
        QImage out(v.width, v.height, QImage::Format_ARGB32);
        if (out.isNull())
            return QImage();
        const size_t rowBytes = static_cast<size_t>(v.width) * 4;
        const size_t outStride = static_cast<size_t>(out.bytesPerLine());
        const size_t vStride = static_cast<size_t>(v.stride());
        if (vStride == outStride && vStride == rowBytes)
        {
            std::memcpy(out.bits(), v.data, static_cast<size_t>(v.height) * rowBytes);
        }
        else
        {
            for (int y = 0; y < v.height; ++y)
            {
                std::memcpy(out.scanLine(y), v.data + static_cast<size_t>(y) * vStride, rowBytes);
            }
        }
        return out;
    }
    case PixelFormat::RGBA32:
    {
        QImage out(v.width, v.height, QImage::Format_ARGB32);
        if (out.isNull())
            return QImage();
        const bool hasAvx2 = mviewer::core::CpuFeatures::hasAvx2();
        const bool hasSsse3 = mviewer::core::CpuFeatures::hasSsse3();
        const size_t vStride = static_cast<size_t>(v.stride());
        for (int y = 0; y < v.height; ++y)
        {
            const uint8_t *sl = v.data + static_cast<size_t>(y) * vStride;
            uint8_t *dl = out.scanLine(y);
            if (hasAvx2)
                convertRgba32ToArgb32RowAVX2(sl, dl, v.width);
            else if (hasSsse3)
                convertRgba32ToArgb32RowSSSE3(sl, dl, v.width);
            else
                convertRgba32ToArgb32RowScalar(sl, dl, v.width);
        }
        return out;
    }
    case PixelFormat::RGB24:
    {
        QImage out(v.width, v.height, QImage::Format_RGB888);
        if (out.isNull())
            return QImage();
        const size_t rowBytes = static_cast<size_t>(v.width) * 3;
        const size_t outStride = static_cast<size_t>(out.bytesPerLine());
        const size_t vStride = static_cast<size_t>(v.stride());
        if (vStride == outStride && vStride == rowBytes)
        {
            std::memcpy(out.bits(), v.data, static_cast<size_t>(v.height) * rowBytes);
        }
        else
        {
            for (int y = 0; y < v.height; ++y)
            {
                std::memcpy(out.scanLine(y), v.data + static_cast<size_t>(y) * vStride, rowBytes);
            }
        }
        return out;
    }
    case PixelFormat::BGR24:
    default:
    {
        QImage out(v.width, v.height, QImage::Format_RGB888);
        if (out.isNull())
            return QImage();
        const size_t vStride = static_cast<size_t>(v.stride());
        for (int y = 0; y < v.height; ++y)
        {
            const uint8_t *sl = v.data + static_cast<size_t>(y) * vStride;
            uint8_t *dl = out.scanLine(y);
            convertBgr24ToRgb888Row(sl, dl, v.width);
        }
        return out;
    }
    }
}

QImage toQImageRef(const ImageData &src)
{
    if (src.isNull() || !src.buffer)
        return QImage();
    const ImageBuffer v = src.view();
    switch (src.format)
    {
    case PixelFormat::RGB24:
        return QImage(v.data, v.width, v.height, v.stride(), QImage::Format_RGB888);
    case PixelFormat::BGR24:
        return QImage(v.data, v.width, v.height, v.stride(), QImage::Format_BGR888);
    case PixelFormat::BGRA32:
        // BGRA byte order == ARGB32 on little-endian (0xAARRGGBB stored BB GG RR AA).
        return QImage(v.data, v.width, v.height, v.stride(), QImage::Format_ARGB32);
    case PixelFormat::Grayscale8:
        return QImage(v.data, v.width, v.height, v.stride(), QImage::Format_Grayscale8);
    case PixelFormat::RGBA32:
        // R,G,B,A order does not map to any Qt format without a channel swap.
        return QImage();
    }
    return QImage();
}

namespace
{

QColorSpace colorSpaceFromIcc(const std::vector<uint8_t> &profile)
{
    if (profile.empty())
        return QColorSpace::SRgb;
    const QByteArray bytes(reinterpret_cast<const char *>(profile.data()),
                           static_cast<qsizetype>(profile.size()));
    const QColorSpace colorSpace = QColorSpace::fromIccProfile(bytes);
    return colorSpace.isValid() ? colorSpace : QColorSpace::SRgb;
}

QColorSpace sourceColorSpace(const mviewer::domain::ImageMetadata &meta)
{
    const auto profileIt = meta.textKeys.find("MViewer.DisplayICC.Base64");
    if (profileIt != meta.textKeys.end() && !profileIt->second.empty())
    {
        const QByteArray encoded(profileIt->second.data(),
                                 static_cast<qsizetype>(profileIt->second.size()));
        const QByteArray profile = QByteArray::fromBase64(encoded);
        const QColorSpace source = QColorSpace::fromIccProfile(profile);
        if (source.isValid())
            return source;
    }
    return QColorSpace::SRgb;
}

} // namespace

QImage toDisplayQImage(const ImageData &src, const mviewer::domain::ImageMetadata &meta)
{
    return toDisplayQImage(src, meta, mviewer::core::DisplayColorContext::sRGB());
}

QImage toDisplayQImage(const ImageData &src, const mviewer::domain::ImageMetadata &meta,
                       const mviewer::core::DisplayColorContext &target)
{
    QImage out = toQImage(src);
    if (out.isNull() || !target.colorManagementEnabled)
        return out;

    const QColorSpace source = sourceColorSpace(meta);
    const QColorSpace destination = colorSpaceFromIcc(target.iccProfile);
    out.setColorSpace(source);
    if (source != destination)
    {
        // QColorSpace performs a direct source -> presentation-target
        // transform. If a platform/plugin cannot build that transform, Qt
        // leaves the raster usable; keep the source-tagged copy rather than
        // returning black or throwing from an asynchronous paint path.
        out.convertToColorSpace(destination);
    }
    if (out.isNull())
        return toQImage(src);
    return out;
}

ImageData toDisplayImageData(const ImageData &src, const mviewer::domain::ImageMetadata &meta)
{
    return toDisplayImageData(src, meta, mviewer::core::DisplayColorContext::sRGB());
}

ImageData toDisplayImageData(const ImageData &src, const mviewer::domain::ImageMetadata &meta,
                             const mviewer::core::DisplayColorContext &target)
{
    return fromQImage(toDisplayQImage(src, meta, target));
}

ImageData fromQImage(const QImage &src)
{
    if (src.isNull())
        return ImageData();

    if (src.format() == QImage::Format_Grayscale8)
    {
        ImageData out = makeImageData(src.width(), src.height(), PixelFormat::Grayscale8);
        const size_t rowBytes = static_cast<size_t>(src.width());
        const size_t srcStride = static_cast<size_t>(src.bytesPerLine());
        const size_t dstStride = out.stride();
        if (srcStride == dstStride && srcStride == rowBytes)
        {
            std::memcpy(out.buffer->data(), src.constBits(),
                        static_cast<size_t>(src.height()) * rowBytes);
        }
        else
        {
            for (int y = 0; y < src.height(); ++y)
            {
                std::memcpy(out.buffer->data() + static_cast<size_t>(y) * dstStride,
                            src.constScanLine(y), rowBytes);
            }
        }
        return out;
    }

    if (src.format() == QImage::Format_RGB888)
    {
        ImageData out = makeImageData(src.width(), src.height(), PixelFormat::RGB24);
        const size_t rowBytes = static_cast<size_t>(src.width()) * 3;
        const size_t srcStride = static_cast<size_t>(src.bytesPerLine());
        const size_t dstStride = out.stride();
        if (srcStride == dstStride && srcStride == rowBytes)
        {
            std::memcpy(out.buffer->data(), src.constBits(),
                        static_cast<size_t>(src.height()) * rowBytes);
        }
        else
        {
            for (int y = 0; y < src.height(); ++y)
            {
                std::memcpy(out.buffer->data() + static_cast<size_t>(y) * dstStride,
                            src.constScanLine(y), rowBytes);
            }
        }
        return out;
    }

    if (src.format() == QImage::Format_ARGB32 || src.format() == QImage::Format_RGB32)
    {
        ImageData out = makeImageData(src.width(), src.height(), PixelFormat::RGB24);
        const size_t dstStride = out.stride();
        uint8_t *dstData = out.buffer->data();
        const size_t w = static_cast<size_t>(src.width());
        for (int y = 0; y < src.height(); ++y)
        {
            const uint8_t *sl = src.constScanLine(y);
            uint8_t *dl = dstData + static_cast<size_t>(y) * dstStride;
            size_t x = 0;
            for (; x + 4 <= w; x += 4)
            {
                const size_t sOff = x * 4;
                const size_t dOff = x * 3;
                const uint8_t *s = sl + sOff;
                uint8_t *d = dl + dOff;
                // Little-endian ARGB32/RGB32 is [B, G, R, A]
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
                d[3] = s[6];
                d[4] = s[5];
                d[5] = s[4];
                d[6] = s[10];
                d[7] = s[9];
                d[8] = s[8];
                d[9] = s[14];
                d[10] = s[13];
                d[11] = s[12];
            }
            for (; x < w; ++x)
            {
                const size_t sOff = x * 4;
                const size_t dOff = x * 3;
                const uint8_t *s = sl + sOff;
                uint8_t *d = dl + dOff;
                d[0] = s[2];
                d[1] = s[1];
                d[2] = s[0];
            }
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
        std::memcpy(out.buffer->data() + static_cast<size_t>(y) * out.stride(),
                    img.constScanLine(y), rowBytes);
    }
    return out;
}

} // namespace mvcore
