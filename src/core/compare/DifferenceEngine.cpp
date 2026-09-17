#include "core/compare/DifferenceEngine.h"
#include "core/simd/CpuFeatures.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstring>

#if defined(__GNUC__) || defined(__clang__)
#define MV_TARGET_AVX2 __attribute__((target("avx2")))
#define MV_TARGET_SSSE3 __attribute__((target("ssse3,sse4.1")))
#else
#define MV_TARGET_AVX2
#define MV_TARGET_SSSE3
#endif

int DifferenceEngine::channelOffset(PixelFormat fmt, int channel)
{
    switch (fmt)
    {
    case PixelFormat::RGB24:
        return channel;
    case PixelFormat::BGR24:
        return 2 - channel;
    case PixelFormat::RGBA32:
        return channel;
    case PixelFormat::BGRA32:
        return 2 - channel;
    case PixelFormat::Grayscale8:
        return 0;
    default:
        return channel;
    }
}

MV_TARGET_SSSE3
static inline void diffRGB24RowSSSE3(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                     uint8_t threshold, bool swapB, int &x)
{
    const __m128i maskR = _mm_setr_epi8(0, 3, 6, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i maskG =
        _mm_setr_epi8(1, 4, 7, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i maskB =
        _mm_setr_epi8(2, 5, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i maskSwapRGB = _mm_setr_epi8(2, 1, 0, 5, 4, 3, 8, 7, 6, 11, 10, 9, -1, -1, -1, -1);
    const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vdiv = _mm_set1_epi16(21846);

    for (; x * 3 + 16 <= w * 3; x += 4)
    {
        const __m128i va =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + static_cast<ptrdiff_t>(x) * 3));
        __m128i vb =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + static_cast<ptrdiff_t>(x) * 3));
        if (swapB)
            vb = _mm_shuffle_epi8(vb, maskSwapRGB);

        const __m128i diff = _mm_or_si128(_mm_subs_epu8(va, vb), _mm_subs_epu8(vb, va));
        const __m128i R = _mm_shuffle_epi8(diff, maskR);
        const __m128i G = _mm_shuffle_epi8(diff, maskG);
        const __m128i B = _mm_shuffle_epi8(diff, maskB);

        const __m128i R16 = _mm_cvtepu8_epi16(R);
        const __m128i G16 = _mm_cvtepu8_epi16(G);
        const __m128i B16 = _mm_cvtepu8_epi16(B);

        const __m128i sum = _mm_add_epi16(_mm_add_epi16(R16, G16), B16);
        const __m128i avg16 = _mm_mulhi_epu16(sum, vdiv);
        const __m128i avg8 = _mm_packus_epi16(avg16, vzero);

        const __m128i under = _mm_subs_epu8(vthresh, avg8);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero);
        const __m128i result = _mm_and_si128(avg8, mask);

        const uint32_t out4 = static_cast<uint32_t>(_mm_cvtsi128_si32(result));
        std::memcpy(dst + x, &out4, 4);
    }
    for (; x + 4 <= w; x += 4)
    {
        alignas(16) uint8_t bufA[16]{};
        alignas(16) uint8_t bufB[16]{};
        std::memcpy(bufA, la + static_cast<ptrdiff_t>(x) * 3, 12);
        std::memcpy(bufB, lb + static_cast<ptrdiff_t>(x) * 3, 12);
        const __m128i va = _mm_load_si128(reinterpret_cast<const __m128i *>(bufA));
        __m128i vb = _mm_load_si128(reinterpret_cast<const __m128i *>(bufB));
        if (swapB)
            vb = _mm_shuffle_epi8(vb, maskSwapRGB);

        const __m128i diff = _mm_or_si128(_mm_subs_epu8(va, vb), _mm_subs_epu8(vb, va));
        const __m128i R = _mm_shuffle_epi8(diff, maskR);
        const __m128i G = _mm_shuffle_epi8(diff, maskG);
        const __m128i B = _mm_shuffle_epi8(diff, maskB);

        const __m128i R16 = _mm_cvtepu8_epi16(R);
        const __m128i G16 = _mm_cvtepu8_epi16(G);
        const __m128i B16 = _mm_cvtepu8_epi16(B);

        const __m128i sum = _mm_add_epi16(_mm_add_epi16(R16, G16), B16);
        const __m128i avg16 = _mm_mulhi_epu16(sum, vdiv);
        const __m128i avg8 = _mm_packus_epi16(avg16, vzero);

        const __m128i under = _mm_subs_epu8(vthresh, avg8);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero);
        const __m128i result = _mm_and_si128(avg8, mask);

        const uint32_t out4 = static_cast<uint32_t>(_mm_cvtsi128_si32(result));
        std::memcpy(dst + x, &out4, 4);
    }
}

static inline void diffRGB24Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                uint8_t threshold, bool useSsse3, bool swapB = false)
{
    int x = 0;
    if (useSsse3)
    {
        diffRGB24RowSSSE3(la, lb, dst, w, threshold, swapB, x);
    }
    const int bR = swapB ? 2 : 0;
    const int bB = swapB ? 0 : 2;
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * 3 + 0]) - static_cast<int>(lb[x * 3 + bR]));
        const int dg = std::abs(static_cast<int>(la[x * 3 + 1]) - static_cast<int>(lb[x * 3 + 1]));
        const int db = std::abs(static_cast<int>(la[x * 3 + 2]) - static_cast<int>(lb[x * 3 + bB]));
        const int sum = dr + dg + db;
        const uint8_t diff = static_cast<uint8_t>((sum * 21846) >> 16);
        dst[x] = (diff >= threshold) ? diff : 0;
    }
}

MV_TARGET_AVX2
static inline void diffGrayscale8RowAVX2(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                         uint8_t threshold, int &x)
{
    const __m256i vthresh = _mm256_set1_epi8(static_cast<char>(threshold));
    const __m256i vzero = _mm256_setzero_si256();
    for (; x + 32 <= w; x += 32)
    {
        const __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(la + x));
        const __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(lb + x));
        const __m256i sub1 = _mm256_subs_epu8(va, vb);
        const __m256i sub2 = _mm256_subs_epu8(vb, va);
        const __m256i diff = _mm256_or_si256(sub1, sub2);
        const __m256i under = _mm256_subs_epu8(vthresh, diff);
        const __m256i mask = _mm256_cmpeq_epi8(under, vzero);
        const __m256i result = _mm256_and_si256(diff, mask);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), result);
    }
}

static inline void diffGrayscale8Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                     uint8_t threshold, bool useAvx2)
{
    int x = 0;
    if (useAvx2)
    {
        diffGrayscale8RowAVX2(la, lb, dst, w, threshold, x);
    }
    const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero128 = _mm_setzero_si128();
    for (; x + 16 <= w; x += 16)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x));
        const __m128i sub1 = _mm_subs_epu8(va, vb);
        const __m128i sub2 = _mm_subs_epu8(vb, va);
        const __m128i diff = _mm_or_si128(sub1, sub2);
        const __m128i under = _mm_subs_epu8(vthresh128, diff);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero128);
        const __m128i result = _mm_and_si128(diff, mask);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), result);
    }
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x]) - static_cast<int>(lb[x]));
        dst[x] = (dr >= threshold) ? static_cast<uint8_t>(dr) : 0;
    }
}

MV_TARGET_SSSE3
static inline void diffRGBA32RowSSSE3(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                      uint8_t threshold, bool swapB, int &x)
{
    const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vdiv = _mm_set1_epi16(21846);
    const __m128i wRGB = _mm_setr_epi8(1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0);
    const __m128i maskSwapRGBA =
        _mm_setr_epi8(2, 1, 0, 3, 6, 5, 4, 7, 10, 9, 8, 11, 14, 13, 12, 15);

    for (; x + 8 <= w; x += 8)
    {
        const __m128i va0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + static_cast<ptrdiff_t>(x) * 4));
        __m128i vb0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + static_cast<ptrdiff_t>(x) * 4));
        const __m128i va1 = _mm_loadu_si128(
            reinterpret_cast<const __m128i *>(la + (static_cast<ptrdiff_t>(x) + 4) * 4));
        __m128i vb1 = _mm_loadu_si128(
            reinterpret_cast<const __m128i *>(lb + (static_cast<ptrdiff_t>(x) + 4) * 4));

        if (swapB)
        {
            vb0 = _mm_shuffle_epi8(vb0, maskSwapRGBA);
            vb1 = _mm_shuffle_epi8(vb1, maskSwapRGBA);
        }

        const __m128i diff0 = _mm_or_si128(_mm_subs_epu8(va0, vb0), _mm_subs_epu8(vb0, va0));
        const __m128i diff1 = _mm_or_si128(_mm_subs_epu8(va1, vb1), _mm_subs_epu8(vb1, va1));

        const __m128i pair_sum0 = _mm_maddubs_epi16(diff0, wRGB);
        const __m128i pair_sum1 = _mm_maddubs_epi16(diff1, wRGB);

        const __m128i pixel_sum = _mm_hadd_epi16(pair_sum0, pair_sum1);
        const __m128i avg16 = _mm_mulhi_epu16(pixel_sum, vdiv);
        const __m128i avg8 = _mm_packus_epi16(avg16, vzero);

        const __m128i under = _mm_subs_epu8(vthresh, avg8);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero);
        const __m128i result = _mm_and_si128(avg8, mask);

        _mm_storel_epi64(reinterpret_cast<__m128i *>(dst + x), result);
    }
    for (; x + 4 <= w; x += 4)
    {
        const __m128i va0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + static_cast<ptrdiff_t>(x) * 4));
        __m128i vb0 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + static_cast<ptrdiff_t>(x) * 4));
        if (swapB)
            vb0 = _mm_shuffle_epi8(vb0, maskSwapRGBA);

        const __m128i diff0 = _mm_or_si128(_mm_subs_epu8(va0, vb0), _mm_subs_epu8(vb0, va0));
        const __m128i pair_sum0 = _mm_maddubs_epi16(diff0, wRGB);
        const __m128i pixel_sum = _mm_hadd_epi16(pair_sum0, pair_sum0);
        const __m128i avg16 = _mm_mulhi_epu16(pixel_sum, vdiv);
        const __m128i avg8 = _mm_packus_epi16(avg16, vzero);
        const __m128i under = _mm_subs_epu8(vthresh, avg8);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero);
        const __m128i result = _mm_and_si128(avg8, mask);
        const uint32_t out4 = static_cast<uint32_t>(_mm_cvtsi128_si32(result));
        std::memcpy(dst + x, &out4, 4);
    }
}

static inline void diffRGBA32Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                 uint8_t threshold, bool useSsse3, bool swapB = false)
{
    int x = 0;
    if (useSsse3)
    {
        diffRGBA32RowSSSE3(la, lb, dst, w, threshold, swapB, x);
    }
    const int bR = swapB ? 2 : 0;
    const int bB = swapB ? 0 : 2;
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * 4 + 0]) - static_cast<int>(lb[x * 4 + bR]));
        const int dg = std::abs(static_cast<int>(la[x * 4 + 1]) - static_cast<int>(lb[x * 4 + 1]));
        const int db = std::abs(static_cast<int>(la[x * 4 + 2]) - static_cast<int>(lb[x * 4 + bB]));
        const int sum = dr + db + dg;
        const uint8_t diff = static_cast<uint8_t>((sum * 21846) >> 16);
        dst[x] = (diff >= threshold) ? diff : 0;
    }
}

static inline void diffScalarRow(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                 int cppA, int cppB, int roA0, int roA1, int roA2, int roB0,
                                 int roB1, int roB2, uint8_t threshold)
{
    const uint8_t *pa = la;
    const uint8_t *pb = lb;
    for (int x = 0; x < w; ++x, pa += cppA, pb += cppB)
    {
        const int dr = std::abs(static_cast<int>(pa[roA0]) - static_cast<int>(pb[roB0]));
        const int dg = std::abs(static_cast<int>(pa[roA1]) - static_cast<int>(pb[roB1]));
        const int db = std::abs(static_cast<int>(pa[roA2]) - static_cast<int>(pb[roB2]));
        const int sum = dr + dg + db;
        const uint8_t diff = static_cast<uint8_t>((sum * 21846) >> 16);
        dst[x] = (diff >= threshold) ? diff : 0;
    }
}

namespace
{

bool isSupportedDiffFormat(PixelFormat fmt)
{
    return fmt == PixelFormat::RGB24 || fmt == PixelFormat::BGR24 || fmt == PixelFormat::RGBA32 ||
           fmt == PixelFormat::BGRA32 || fmt == PixelFormat::Grayscale8;
}

void diffOneRow(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w, bool isGray8,
                bool isRGB24Pair, bool isRGBA32Pair, bool useAvx2, bool useSsse3, bool swapB24,
                bool swapB32, int cppA, int cppB, int roA0, int roA1, int roA2, int roB0, int roB1,
                int roB2, uint8_t threshold)
{
    if (isGray8)
        diffGrayscale8Row(la, lb, dst, w, threshold, useAvx2);
    else if (isRGB24Pair)
        diffRGB24Row(la, lb, dst, w, threshold, useSsse3, swapB24);
    else if (isRGBA32Pair)
        diffRGBA32Row(la, lb, dst, w, threshold, useSsse3, swapB32);
    else
        diffScalarRow(la, lb, dst, w, cppA, cppB, roA0, roA1, roA2, roB0, roB1, roB2, threshold);
}

} // namespace

ImageData DifferenceEngine::differenceMap(const ImageData &a, const ImageData &b, uint8_t threshold)
{
    if (a.isNull() || b.isNull() || !isSupportedDiffFormat(a.format) ||
        !isSupportedDiffFormat(b.format))
        return ImageData();

    const int w = std::min(a.width, b.width);
    const int h = std::min(a.height, b.height);
    const int cppA = a.channelsPerPixel();
    const int cppB = b.channelsPerPixel();
    const int roA0 = channelOffset(a.format, 0);
    const int roA1 = channelOffset(a.format, 1);
    const int roA2 = channelOffset(a.format, 2);
    const int roB0 = channelOffset(b.format, 0);
    const int roB1 = channelOffset(b.format, 1);
    const int roB2 = channelOffset(b.format, 2);

    ImageData out = makeImageData(w, h, PixelFormat::Grayscale8);
    if (out.isNull())
        return ImageData();

    const bool isGray8 =
        (a.format == PixelFormat::Grayscale8 && b.format == PixelFormat::Grayscale8);
    const bool aIsRGB24 = (a.format == PixelFormat::RGB24 || a.format == PixelFormat::BGR24);
    const bool bIsRGB24 = (b.format == PixelFormat::RGB24 || b.format == PixelFormat::BGR24);
    const bool isRGB24Pair = aIsRGB24 && bIsRGB24;
    const bool aIsRGBA32 = (a.format == PixelFormat::RGBA32 || a.format == PixelFormat::BGRA32);
    const bool bIsRGBA32 = (b.format == PixelFormat::RGBA32 || b.format == PixelFormat::BGRA32);
    const bool isRGBA32Pair = aIsRGBA32 && bIsRGBA32;
    const bool swapB24 = (a.format != b.format);
    const bool swapB32 = (a.format != b.format);

    const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
    const bool useSsse3 = mviewer::core::CpuFeatures::hasSsse3();

    if (isGray8 && a.stride() == static_cast<size_t>(w) && b.stride() == static_cast<size_t>(w) &&
        out.stride() == static_cast<size_t>(w))
    {
        diffGrayscale8Row(a.buffer->data(), b.buffer->data(), out.buffer->data(), w * h, threshold,
                          useAvx2);
        return out;
    }

    for (int y = 0; y < h; ++y)
    {
        const uint8_t *la = a.buffer->data() + static_cast<size_t>(y) * a.stride();
        const uint8_t *lb = b.buffer->data() + static_cast<size_t>(y) * b.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        diffOneRow(la, lb, dst, w, isGray8, isRGB24Pair, isRGBA32Pair, useAvx2, useSsse3, swapB24,
                   swapB32, cppA, cppB, roA0, roA1, roA2, roB0, roB1, roB2, threshold);
    }
    return out;
}

MV_TARGET_AVX2
static inline void applyThresholdAVX2Span(const uint8_t *src, uint8_t *dst, size_t total,
                                          uint8_t threshold, size_t &x)
{
    const __m256i vthresh = _mm256_set1_epi8(static_cast<char>(threshold));
    const __m256i vzero = _mm256_setzero_si256();
    for (; x + 32 <= total; x += 32)
    {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x));
        const __m256i under = _mm256_subs_epu8(vthresh, v);
        const __m256i mask = _mm256_cmpeq_epi8(under, vzero);
        const __m256i result = _mm256_and_si256(v, mask);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), result);
    }
}

ImageData DifferenceEngine::applyThreshold(const ImageData &gray, uint8_t threshold)
{
    if (gray.isNull())
        return ImageData();
    ImageData out = makeImageData(gray.width, gray.height, gray.format);
    if (out.isNull())
        return ImageData();
    const int cpp = gray.channelsPerPixel();
    const int ro = channelOffset(gray.format, 0);

    if (cpp == 1 && ro == 0)
    {
        const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
        if (gray.stride() == static_cast<size_t>(gray.width) &&
            out.stride() == static_cast<size_t>(out.width))
        {
            const size_t total = static_cast<size_t>(gray.width) * gray.height;
            const uint8_t *src = gray.buffer->data();
            uint8_t *dst = out.buffer->data();
            size_t x = 0;
            if (useAvx2)
            {
                applyThresholdAVX2Span(src, dst, total, threshold, x);
            }
            const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
            const __m128i vzero128 = _mm_setzero_si128();
            for (; x + 16 <= total; x += 16)
            {
                const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + x));
                const __m128i under = _mm_subs_epu8(vthresh128, v);
                const __m128i mask = _mm_cmpeq_epi8(under, vzero128);
                const __m128i result = _mm_and_si128(v, mask);
                _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), result);
            }
            for (; x < total; ++x)
            {
                const uint8_t v = src[x];
                dst[x] = (v >= threshold) ? v : 0;
            }
            return out;
        }

        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            size_t x = 0;
            if (useAvx2)
            {
                applyThresholdAVX2Span(src, dst, static_cast<size_t>(gray.width), threshold, x);
            }
            const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
            const __m128i vzero128 = _mm_setzero_si128();
            for (; x + 16 <= static_cast<size_t>(gray.width); x += 16)
            {
                const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + x));
                const __m128i under = _mm_subs_epu8(vthresh128, v);
                const __m128i mask = _mm_cmpeq_epi8(under, vzero128);
                const __m128i result = _mm_and_si128(v, mask);
                _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), result);
            }
            for (; x < static_cast<size_t>(gray.width); ++x)
            {
                const uint8_t v = src[x];
                dst[x] = (v >= threshold) ? v : 0;
            }
        }
        return out;
    }

    for (int y = 0; y < gray.height; ++y)
    {
        const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride() + ro;
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < gray.width; ++x, src += cpp)
        {
            const uint8_t v = *src;
            dst[x] = (v >= threshold) ? v : 0;
        }
    }
    return out;
}

ImageData DifferenceEngine::amplify(const ImageData &gray, double gain)
{
    if (gray.isNull())
        return ImageData();
    if (gain <= 1.0)
    {
        ImageData copy = makeImageData(gray.width, gray.height, gray.format);
        if (copy.isNull())
            return ImageData();
        if (gray.stride() == copy.stride())
        {
            std::memcpy(copy.buffer->data(), gray.buffer->data(),
                        static_cast<size_t>(gray.stride()) * gray.height);
        }
        else
        {
            for (int y = 0; y < gray.height; ++y)
            {
                const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
                uint8_t *dst = copy.buffer->data() + static_cast<size_t>(y) * copy.stride();
                std::memcpy(dst, src, static_cast<size_t>(gray.width) * gray.channelsPerPixel());
            }
        }
        return copy;
    }

    ImageData out = makeImageData(gray.width, gray.height, gray.format);
    if (out.isNull())
        return ImageData();

    const int cpp = gray.channelsPerPixel();
    const int ro = channelOffset(gray.format, 0);

    alignas(16) uint8_t lut[256];
    for (int i = 0; i < 256; ++i)
    {
        lut[i] = static_cast<uint8_t>(std::min(255, static_cast<int>(std::round(i * gain))));
    }

    if (cpp == 1 && ro == 0)
    {
        if (gray.stride() == static_cast<size_t>(gray.width) &&
            out.stride() == static_cast<size_t>(out.width))
        {
            const size_t total = static_cast<size_t>(gray.width) * gray.height;
            const uint8_t *src = gray.buffer->data();
            uint8_t *dst = out.buffer->data();
            for (size_t i = 0; i < total; ++i)
                dst[i] = lut[src[i]];
        }
        else
        {
            for (int y = 0; y < gray.height; ++y)
            {
                const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
                uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
                for (int x = 0; x < gray.width; ++x)
                    dst[x] = lut[src[x]];
            }
        }
    }
    else
    {
        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride() + ro;
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < gray.width; ++x, src += cpp)
            {
                const uint8_t v = lut[*src];
                for (int c = 0; c < cpp; ++c)
                    dst[x * cpp + c] = v;
            }
        }
    }
    return out;
}
