#include "core/compare/DifferenceEngine.h"
#include "core/simd/CpuFeatures.h"

#include <algorithm>
#include <array>
#include <cstring>

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

namespace
{
// Fixed-point reciprocal for division by 3: round((1.0 / 3.0) * 65536) = 21846
constexpr uint16_t kFixedPointDiv3Multiplier = 21846;
} // namespace

#if defined(MV_ARCH_X86)
MV_TARGET_SSSE3_SSE41
static inline uint32_t diffRGB24Quad(__m128i va, __m128i vb, __m128i maskR, __m128i maskG,
                                     __m128i maskB, __m128i vthresh, __m128i vdiv, __m128i vzero)
{
    const __m128i diff = _mm_or_si128(_mm_subs_epu8(va, vb), _mm_subs_epu8(vb, va));
    const __m128i R16 = _mm_cvtepu8_epi16(_mm_shuffle_epi8(diff, maskR));
    const __m128i G16 = _mm_cvtepu8_epi16(_mm_shuffle_epi8(diff, maskG));
    const __m128i B16 = _mm_cvtepu8_epi16(_mm_shuffle_epi8(diff, maskB));
    const __m128i sum = _mm_add_epi16(_mm_add_epi16(R16, G16), B16);
    const __m128i avg8 = _mm_packus_epi16(_mm_mulhi_epu16(sum, vdiv), vzero);
    const __m128i mask = _mm_cmpeq_epi8(_mm_subs_epu8(vthresh, avg8), vzero);
    return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_and_si128(avg8, mask)));
}

MV_TARGET_SSSE3_SSE41
static inline int diffRGB24RowSIMD(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                   uint8_t threshold)
{
    const __m128i maskR =
        _mm_setr_epi8(0, 3, 6, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i maskG =
        _mm_setr_epi8(1, 4, 7, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i maskB =
        _mm_setr_epi8(2, 5, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
    const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vdiv = _mm_set1_epi16(kFixedPointDiv3Multiplier);

    int x = 0;
    for (; x * 3 + 16 <= w * 3; x += 4)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x * 3));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x * 3));
        const uint32_t out4 = diffRGB24Quad(va, vb, maskR, maskG, maskB, vthresh, vdiv, vzero);
        std::memcpy(dst + x, &out4, 4);
    }
    return x;
}
#endif

static inline void diffRGB24Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                uint8_t threshold, bool useSse41Ssse3)
{
    int x = 0;
#if defined(MV_ARCH_X86)
    if (useSse41Ssse3)
    {
        x = diffRGB24RowSIMD(la, lb, dst, w, threshold);
    }
#else
    (void)useSse41Ssse3;
#endif
    for (; x + 4 <= w; x += 4)
    {
        for (int k = 0; k < 4; ++k)
        {
            const int idx = (x + k) * 3;
            const int dr =
                std::abs(static_cast<int>(la[idx + 0]) - static_cast<int>(lb[idx + 0]));
            const int dg =
                std::abs(static_cast<int>(la[idx + 1]) - static_cast<int>(lb[idx + 1]));
            const int db =
                std::abs(static_cast<int>(la[idx + 2]) - static_cast<int>(lb[idx + 2]));
            const int sum = dr + dg + db;
            const uint8_t diff = static_cast<uint8_t>((sum * kFixedPointDiv3Multiplier) >> 16);
            dst[x + k] = (diff >= threshold) ? diff : 0;
        }
    }
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * 3 + 0]) - static_cast<int>(lb[x * 3 + 0]));
        const int dg = std::abs(static_cast<int>(la[x * 3 + 1]) - static_cast<int>(lb[x * 3 + 1]));
        const int db = std::abs(static_cast<int>(la[x * 3 + 2]) - static_cast<int>(lb[x * 3 + 2]));
        const int sum = dr + dg + db;
        const uint8_t diff = static_cast<uint8_t>((sum * kFixedPointDiv3Multiplier) >> 16);
        dst[x] = (diff >= threshold) ? diff : 0;
    }
}

#if defined(MV_ARCH_X86)
MV_TARGET_AVX2
static inline int diffGrayscale8RowAVX2(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                       uint8_t threshold)
{
    int x = 0;
    const __m256i vthresh = _mm256_set1_epi8(static_cast<char>(threshold));
    const __m256i vzero = _mm256_setzero_si256();
    for (; x + 32 <= w; x += 32)
    {
        const __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(la + x));
        const __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(lb + x));
        const __m256i diff =
            _mm256_or_si256(_mm256_subs_epu8(va, vb), _mm256_subs_epu8(vb, va));
        const __m256i under = _mm256_subs_epu8(vthresh, diff);
        const __m256i mask = _mm256_cmpeq_epi8(under, vzero);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), _mm256_and_si256(diff, mask));
    }
    return x;
}

MV_TARGET_SSE2
static inline int diffGrayscale8RowSSE2(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                       int startX, uint8_t threshold)
{
    int x = startX;
    const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero128 = _mm_setzero_si128();
    for (; x + 16 <= w; x += 16)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x));
        const __m128i diff = _mm_or_si128(_mm_subs_epu8(va, vb), _mm_subs_epu8(vb, va));
        const __m128i under = _mm_subs_epu8(vthresh128, diff);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero128);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), _mm_and_si128(diff, mask));
    }
    return x;
}
#endif

static inline void diffGrayscale8Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                     uint8_t threshold, bool useAvx2)
{
    int x = 0;
#if defined(MV_ARCH_X86)
    if (useAvx2)
        x = diffGrayscale8RowAVX2(la, lb, dst, w, threshold);
    x = diffGrayscale8RowSSE2(la, lb, dst, w, x, threshold);
#else
    (void)useAvx2;
#endif
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x]) - static_cast<int>(lb[x]));
        dst[x] = (dr >= threshold) ? static_cast<uint8_t>(dr) : 0;
    }
}

#if defined(MV_ARCH_X86)
MV_TARGET_SSSE3
static inline uint32_t diffRGBA32Quad(__m128i va, __m128i vb, __m128i wRGB, __m128i vthresh,
                                      __m128i vdiv, __m128i vzero)
{
    const __m128i diff = _mm_or_si128(_mm_subs_epu8(va, vb), _mm_subs_epu8(vb, va));
    const __m128i pair_sum = _mm_maddubs_epi16(diff, wRGB);
    const __m128i pixel_sum = _mm_hadd_epi16(pair_sum, pair_sum);
    const __m128i avg8 = _mm_packus_epi16(_mm_mulhi_epu16(pixel_sum, vdiv), vzero);
    const __m128i mask = _mm_cmpeq_epi8(_mm_subs_epu8(vthresh, avg8), vzero);
    return static_cast<uint32_t>(_mm_cvtsi128_si32(_mm_and_si128(avg8, mask)));
}

MV_TARGET_SSSE3
static inline int diffRGBA32RowSIMD(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                    uint8_t threshold)
{
    const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vdiv = _mm_set1_epi16(kFixedPointDiv3Multiplier);
    const __m128i wRGB = _mm_setr_epi8(1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0);

    int x = 0;
    for (; x + 8 <= w; x += 8)
    {
        const __m128i va0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x * 4));
        const __m128i vb0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x * 4));
        const __m128i va1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + (x + 4) * 4));
        const __m128i vb1 =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + (x + 4) * 4));

        const __m128i diff0 = _mm_or_si128(_mm_subs_epu8(va0, vb0), _mm_subs_epu8(vb0, va0));
        const __m128i diff1 = _mm_or_si128(_mm_subs_epu8(va1, vb1), _mm_subs_epu8(vb1, va1));

        const __m128i pixel_sum =
            _mm_hadd_epi16(_mm_maddubs_epi16(diff0, wRGB), _mm_maddubs_epi16(diff1, wRGB));
        const __m128i avg8 = _mm_packus_epi16(_mm_mulhi_epu16(pixel_sum, vdiv), vzero);
        const __m128i mask = _mm_cmpeq_epi8(_mm_subs_epu8(vthresh, avg8), vzero);

        _mm_storel_epi64(reinterpret_cast<__m128i *>(dst + x), _mm_and_si128(avg8, mask));
    }
    for (; x + 4 <= w; x += 4)
    {
        const __m128i va0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x * 4));
        const __m128i vb0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x * 4));
        const uint32_t out4 = diffRGBA32Quad(va0, vb0, wRGB, vthresh, vdiv, vzero);
        std::memcpy(dst + x, &out4, 4);
    }
    return x;
}
#endif

static inline void diffRGBA32Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                 uint8_t threshold, bool useSsse3)
{
    int x = 0;
#if defined(MV_ARCH_X86)
    if (useSsse3)
    {
        x = diffRGBA32RowSIMD(la, lb, dst, w, threshold);
    }
#else
    (void)useSsse3;
#endif
    for (; x + 4 <= w; x += 4)
    {
        for (int k = 0; k < 4; ++k)
        {
            const int idx = (x + k) * 4;
            const int dr =
                std::abs(static_cast<int>(la[idx + 0]) - static_cast<int>(lb[idx + 0]));
            const int dg =
                std::abs(static_cast<int>(la[idx + 1]) - static_cast<int>(lb[idx + 1]));
            const int db =
                std::abs(static_cast<int>(la[idx + 2]) - static_cast<int>(lb[idx + 2]));
            const int sum = dr + db + dg;
            const uint8_t diff = static_cast<uint8_t>((sum * kFixedPointDiv3Multiplier) >> 16);
            dst[x + k] = (diff >= threshold) ? diff : 0;
        }
    }
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * 4 + 0]) - static_cast<int>(lb[x * 4 + 0]));
        const int dg = std::abs(static_cast<int>(la[x * 4 + 1]) - static_cast<int>(lb[x * 4 + 1]));
        const int db = std::abs(static_cast<int>(la[x * 4 + 2]) - static_cast<int>(lb[x * 4 + 2]));
        const int sum = dr + db + dg;
        const uint8_t diff = static_cast<uint8_t>((sum * kFixedPointDiv3Multiplier) >> 16);
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
        const uint8_t diff = static_cast<uint8_t>((sum * kFixedPointDiv3Multiplier) >> 16);
        dst[x] = (diff >= threshold) ? diff : 0;
    }
}

ImageData DifferenceEngine::differenceMap(const ImageData &a, const ImageData &b, uint8_t threshold)
{
    if (a.isNull() || b.isNull())
        return ImageData();
    if (a.format != PixelFormat::RGB24 && a.format != PixelFormat::BGR24 &&
        a.format != PixelFormat::RGBA32 && a.format != PixelFormat::BGRA32 &&
        a.format != PixelFormat::Grayscale8)
        return ImageData();
    if (b.format != PixelFormat::RGB24 && b.format != PixelFormat::BGR24 &&
        b.format != PixelFormat::RGBA32 && b.format != PixelFormat::BGRA32 &&
        b.format != PixelFormat::Grayscale8)
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

    const bool isRGB24 = (a.format == b.format) &&
                         (a.format == PixelFormat::RGB24 || a.format == PixelFormat::BGR24);
    const bool isRGBA32 = (a.format == b.format) &&
                          (a.format == PixelFormat::RGBA32 || a.format == PixelFormat::BGRA32);
    const bool isGray8 =
        (a.format == PixelFormat::Grayscale8 && b.format == PixelFormat::Grayscale8);

    const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
    const bool useSsse3 = mviewer::core::CpuFeatures::hasSsse3();
    const bool useSse41Ssse3 = useSsse3 && mviewer::core::CpuFeatures::hasSse41();

    for (int y = 0; y < h; ++y)
    {
        const uint8_t *la = a.buffer->data() + static_cast<size_t>(y) * a.stride();
        const uint8_t *lb = b.buffer->data() + static_cast<size_t>(y) * b.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();

        if (isGray8)
            diffGrayscale8Row(la, lb, dst, w, threshold, useAvx2);
        else if (isRGB24)
            diffRGB24Row(la, lb, dst, w, threshold, useSse41Ssse3);
        else if (isRGBA32)
            diffRGBA32Row(la, lb, dst, w, threshold, useSsse3);
        else
            diffScalarRow(la, lb, dst, w, cppA, cppB, roA0, roA1, roA2, roB0, roB1, roB2,
                          threshold);
    }
    return out;
}

#if defined(MV_ARCH_X86)
MV_TARGET_AVX2
static inline int applyThresholdRowAVX2(const uint8_t *src, uint8_t *dst, int width,
                                        uint8_t threshold)
{
    int x = 0;
    const __m256i vthresh = _mm256_set1_epi8(static_cast<char>(threshold));
    const __m256i vzero = _mm256_setzero_si256();
    for (; x + 32 <= width; x += 32)
    {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x));
        const __m256i mask = _mm256_cmpeq_epi8(_mm256_subs_epu8(vthresh, v), vzero);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), _mm256_and_si256(v, mask));
    }
    return x;
}

MV_TARGET_SSE2
static inline int applyThresholdRowSSE2(const uint8_t *src, uint8_t *dst, int startX, int width,
                                        uint8_t threshold)
{
    int x = startX;
    const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero128 = _mm_setzero_si128();
    for (; x + 16 <= width; x += 16)
    {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + x));
        const __m128i mask = _mm_cmpeq_epi8(_mm_subs_epu8(vthresh128, v), vzero128);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), _mm_and_si128(v, mask));
    }
    return x;
}
#endif

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
#if defined(MV_ARCH_X86)
        const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            int x = 0;
            if (useAvx2)
                x = applyThresholdRowAVX2(src, dst, gray.width, threshold);
            x = applyThresholdRowSSE2(src, dst, x, gray.width, threshold);
            for (; x < gray.width; ++x)
            {
                const uint8_t v = src[x];
                dst[x] = (v >= threshold) ? v : 0;
            }
        }
        return out;
#else
        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < gray.width; ++x)
            {
                const uint8_t v = src[x];
                dst[x] = (v >= threshold) ? v : 0;
            }
        }
        return out;
#endif
    }

    for (int y = 0; y < gray.height; ++y)
    {
        const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        for (int x = 0; x < gray.width; ++x)
        {
            const uint8_t v = src[x * cpp + ro];
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
        return gray;

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
        if (gray.stride() == static_cast<ptrdiff_t>(gray.width) &&
            out.stride() == static_cast<ptrdiff_t>(out.width))
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
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < gray.width; ++x)
            {
                const uint8_t v = lut[src[x * cpp + ro]];
                for (int c = 0; c < cpp; ++c)
                    dst[x * cpp + c] = v;
            }
        }
    }
    return out;
}

DifferenceEngine::DiffStats DifferenceEngine::computeStats(const ImageData &grayDiff,
                                                           uint8_t threshold)
{
    if (grayDiff.isNull())
        return DiffStats{};
    return computeStats(grayDiff, threshold, 0, 0, grayDiff.width, grayDiff.height);
}

#include "DifferenceEngine_simd.inc"

static inline void accumulateGrayscaleStatsScalar(const uint8_t *data, size_t count, int minDiff,
                                                  int64_t &sum, int64_t &diffCount, int &maxV,
                                                  int64_t &sumSq)
{
    for (size_t i = 0; i < count; ++i)
    {
        const int val = data[i];
        sum += val;
        diffCount += (val >= minDiff ? 1 : 0);
        maxV = std::max(maxV, val);
        sumSq += static_cast<int64_t>(val) * val;
    }
}

DifferenceEngine::DiffStats DifferenceEngine::computeStats(const ImageData &grayDiff,
                                                           uint8_t threshold, int roiX, int roiY,
                                                           int roiW, int roiH)
{
    DiffStats s;
    if (grayDiff.isNull() || roiW <= 0 || roiH <= 0)
        return s;

    // 64-bit coordinate clamping prevents integer overflow
    const int64_t x0 = std::clamp<int64_t>(roiX, 0, grayDiff.width);
    const int64_t y0 = std::clamp<int64_t>(roiY, 0, grayDiff.height);
    const int64_t x1 = std::clamp<int64_t>(static_cast<int64_t>(roiX) + roiW, 0, grayDiff.width);
    const int64_t y1 = std::clamp<int64_t>(static_cast<int64_t>(roiY) + roiH, 0, grayDiff.height);
    if (x0 >= x1 || y0 >= y1)
        return s;

    const int cpp = grayDiff.channelsPerPixel();
    const int ro = channelOffset(grayDiff.format, 0);
    const int minDiff = std::max<int>(threshold, 1);

    int64_t sum = 0;
    int64_t diffCount = 0;
    int maxV = 0;
    int64_t sumSq = 0;
    const bool contiguous =
        (cpp == 1 && ro == 0 && x0 == 0 && y0 == 0 && x1 == grayDiff.width &&
         y1 == grayDiff.height && grayDiff.stride() == static_cast<ptrdiff_t>(grayDiff.width));
#if defined(MV_ARCH_X86)
    const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
    const bool useSSE2 = mviewer::core::CpuFeatures::hasSSE2();
#endif

    if (contiguous)
    {
        const size_t total = static_cast<size_t>(grayDiff.width) * grayDiff.height;
        const uint8_t *src = grayDiff.buffer->data();
#if defined(MV_ARCH_X86)
        if (useAvx2)
            accumulateGrayscaleStatsAVX2(src, total, minDiff, sum, diffCount, maxV, sumSq);
        else if (useSSE2)
            accumulateGrayscaleStatsSSE2(src, total, minDiff, sum, diffCount, maxV, sumSq);
        else
#endif
            accumulateGrayscaleStatsScalar(src, total, minDiff, sum, diffCount, maxV, sumSq);
    }
    else if (cpp == 1 && ro == 0)
    {
        const size_t rowLen = static_cast<size_t>(x1 - x0);
        for (int64_t y = y0; y < y1; ++y)
        {
            const uint8_t *src = grayDiff.buffer->data() +
                                 static_cast<size_t>(y) * grayDiff.stride() +
                                 static_cast<size_t>(x0);
#if defined(MV_ARCH_X86)
            if (useAvx2)
                accumulateGrayscaleStatsAVX2(src, rowLen, minDiff, sum, diffCount, maxV, sumSq);
            else if (useSSE2)
                accumulateGrayscaleStatsSSE2(src, rowLen, minDiff, sum, diffCount, maxV, sumSq);
            else
#endif
                accumulateGrayscaleStatsScalar(src, rowLen, minDiff, sum, diffCount, maxV, sumSq);
        }
    }
    else
    {
        for (int64_t y = y0; y < y1; ++y)
        {
            const uint8_t *src = grayDiff.buffer->data() +
                                 static_cast<size_t>(y) * grayDiff.stride() +
                                 static_cast<size_t>(x0) * cpp + ro;
            for (int64_t x = x0; x < x1; ++x, src += cpp)
            {
                const int v = *src;
                sum += v;
                diffCount += (v >= minDiff ? 1 : 0);
                maxV = std::max(maxV, v);
                sumSq += static_cast<int64_t>(v) * v;
            }
        }
    }
    const int64_t count = (x1 - x0) * (y1 - y0);
    s.totalPixels = count;
    s.diffPixels = diffCount;
    s.diffRatio = count > 0 ? static_cast<double>(diffCount) / static_cast<double>(count) : 0.0;
    s.meanDiff = count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    s.maxDiff = maxV;
    const double variance =
        count > 0 ? (static_cast<double>(sumSq) / count) - (s.meanDiff * s.meanDiff) : 0.0;
    s.stdDevDiff = std::sqrt(std::max(0.0, variance));
    return s;
}

uint8_t DifferenceEngine::suggestThreshold(const ImageData &grayDiff)
{
    if (grayDiff.isNull() || grayDiff.width <= 0 || grayDiff.height <= 0)
        return 0;

    int hist[256] = {0};
    const int w = grayDiff.width;
    const int h = grayDiff.height;
    const int cpp = grayDiff.channelsPerPixel();
    const int ro = channelOffset(grayDiff.format, 0);

    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
        for (int x = 0; x < w; ++x)
        {
            const uint8_t v = (cpp == 1 && ro == 0) ? src[x] : src[x * cpp + ro];
            ++hist[v];
        }
    }

    const int total = w * h;
    const int nonZero = total - hist[0];
    if (nonZero <= 0)
        return 0;

    double sum = 0.0;
    for (int i = 1; i < 256; ++i)
        sum += static_cast<double>(i) * hist[i];

    double sumB = 0.0;
    int wB = 0;
    double maxVar = -1.0;
    int bestT = 1;

    for (int t = 1; t < 255; ++t)
    {
        wB += hist[t];
        if (wB == 0)
            continue;
        const int wF = nonZero - wB;
        if (wF == 0)
            break;

        sumB += static_cast<double>(t) * hist[t];
        const double mB = sumB / wB;
        const double mF = (sum - sumB) / wF;

        const double varBetween =
            static_cast<double>(wB) * static_cast<double>(wF) * (mB - mF) * (mB - mF);
        if (varBetween > maxVar)
        {
            maxVar = varBetween;
            bestT = t;
        }
    }

    return static_cast<uint8_t>(std::clamp(bestT, 1, 254));
}

