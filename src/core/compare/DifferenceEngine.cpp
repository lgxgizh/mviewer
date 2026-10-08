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

static inline void diffRGB24Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                uint8_t threshold, bool useSsse3)
{
    int x = 0;
    if (useSsse3)
    {
        const __m128i maskR =
            _mm_setr_epi8(0, 3, 6, 9, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
        const __m128i maskG =
            _mm_setr_epi8(1, 4, 7, 10, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
        const __m128i maskB =
            _mm_setr_epi8(2, 5, 8, 11, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1);
        const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
        const __m128i vzero = _mm_setzero_si128();
        const __m128i vdiv = _mm_set1_epi16(21846);

        for (; x * 3 + 16 <= w * 3; x += 4)
        {
            const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x * 3));
            const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x * 3));
            const uint32_t out4 = diffRGB24Quad(va, vb, maskR, maskG, maskB, vthresh, vdiv, vzero);
            std::memcpy(dst + x, &out4, 4);
        }
    }
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * 3 + 0]) - static_cast<int>(lb[x * 3 + 0]));
        const int dg = std::abs(static_cast<int>(la[x * 3 + 1]) - static_cast<int>(lb[x * 3 + 1]));
        const int db = std::abs(static_cast<int>(la[x * 3 + 2]) - static_cast<int>(lb[x * 3 + 2]));
        const int sum = dr + dg + db;
        const uint8_t diff = static_cast<uint8_t>((sum * 21846) >> 16);
        dst[x] = (diff >= threshold) ? diff : 0;
    }
}

static inline void diffGrayscale8Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                     uint8_t threshold, bool useAvx2)
{
    int x = 0;
    if (useAvx2)
    {
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
    }
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
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x]) - static_cast<int>(lb[x]));
        dst[x] = (dr >= threshold) ? static_cast<uint8_t>(dr) : 0;
    }
}

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

static inline void diffRGBA32Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                 uint8_t threshold, bool useSsse3)
{
    int x = 0;
    if (useSsse3)
    {
        const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
        const __m128i vzero = _mm_setzero_si128();
        const __m128i vdiv = _mm_set1_epi16(21846);
        const __m128i wRGB = _mm_setr_epi8(1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0, 1, 1, 1, 0);

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
    }
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * 4 + 0]) - static_cast<int>(lb[x * 4 + 0]));
        const int dg = std::abs(static_cast<int>(la[x * 4 + 1]) - static_cast<int>(lb[x * 4 + 1]));
        const int db = std::abs(static_cast<int>(la[x * 4 + 2]) - static_cast<int>(lb[x * 4 + 2]));
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

    for (int y = 0; y < h; ++y)
    {
        const uint8_t *la = a.buffer->data() + static_cast<size_t>(y) * a.stride();
        const uint8_t *lb = b.buffer->data() + static_cast<size_t>(y) * b.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();

        if (isGray8)
            diffGrayscale8Row(la, lb, dst, w, threshold, useAvx2);
        else if (isRGB24)
            diffRGB24Row(la, lb, dst, w, threshold, useSsse3);
        else if (isRGBA32)
            diffRGBA32Row(la, lb, dst, w, threshold, useSsse3);
        else
            diffScalarRow(la, lb, dst, w, cppA, cppB, roA0, roA1, roA2, roB0, roB1, roB2,
                          threshold);
    }
    return out;
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
        const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
        const __m128i vzero128 = _mm_setzero_si128();
#if defined(__AVX2__) || defined(_M_AVX2)
        const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
        const __m256i vthresh256 = _mm256_set1_epi8(static_cast<char>(threshold));
        const __m256i vzero256 = _mm256_setzero_si256();
#endif
        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            int x = 0;
#if defined(__AVX2__) || defined(_M_AVX2)
            if (useAvx2)
            {
                for (; x + 32 <= gray.width; x += 32)
                {
                    const __m256i v =
                        _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x));
                    const __m256i mask =
                        _mm256_cmpeq_epi8(_mm256_subs_epu8(vthresh256, v), vzero256);
                    _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x),
                                        _mm256_and_si256(v, mask));
                }
            }
#endif
            for (; x + 16 <= gray.width; x += 16)
            {
                const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + x));
                const __m128i mask = _mm_cmpeq_epi8(_mm_subs_epu8(vthresh128, v), vzero128);
                _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), _mm_and_si128(v, mask));
            }
            for (; x < gray.width; ++x)
            {
                const uint8_t v = src[x];
                dst[x] = (v >= threshold) ? v : 0;
            }
        }
        return out;
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

#if defined(__AVX2__) || defined(_M_AVX2)
static inline void accumulateGrayscaleStatsAVX2(const uint8_t *data, size_t count, int minDiff,
                                                int64_t &sum, int64_t &diffCount, int &maxV,
                                                int64_t &sumSq)
{
    const __m256i vzero = _mm256_setzero_si256();
    const __m256i vMinDiff = _mm256_set1_epi8(static_cast<char>(minDiff));
    const __m256i vOnes = _mm256_set1_epi8(1);
    __m256i vSumAcc = _mm256_setzero_si256();
    __m256i vDiffAcc = _mm256_setzero_si256();
    __m256i vMaxAcc = _mm256_setzero_si256();
    __m256i vSumSqAcc = _mm256_setzero_si256();

    size_t i = 0;
    for (; i + 32 <= count; i += 32)
    {
        const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i));
        vMaxAcc = _mm256_max_epu8(vMaxAcc, v);

        const __m256i sad = _mm256_sad_epu8(v, vzero);
        vSumAcc = _mm256_add_epi64(vSumAcc, sad);

        const __m256i under = _mm256_subs_epu8(vMinDiff, v);
        const __m256i mask = _mm256_cmpeq_epi8(under, vzero);
        const __m256i matchedOnes = _mm256_and_si256(mask, vOnes);
        const __m256i diffSad = _mm256_sad_epu8(matchedOnes, vzero);
        vDiffAcc = _mm256_add_epi64(vDiffAcc, diffSad);

        const __m256i vlo = _mm256_unpacklo_epi8(v, vzero);
        const __m256i vhi = _mm256_unpackhi_epi8(v, vzero);
        const __m256i sqlo = _mm256_madd_epi16(vlo, vlo);
        const __m256i sqhi = _mm256_madd_epi16(vhi, vhi);
        vSumSqAcc = _mm256_add_epi64(vSumSqAcc, _mm256_unpacklo_epi32(sqlo, vzero));
        vSumSqAcc = _mm256_add_epi64(vSumSqAcc, _mm256_unpackhi_epi32(sqlo, vzero));
        vSumSqAcc = _mm256_add_epi64(vSumSqAcc, _mm256_unpacklo_epi32(sqhi, vzero));
        vSumSqAcc = _mm256_add_epi64(vSumSqAcc, _mm256_unpackhi_epi32(sqhi, vzero));
    }

    alignas(32) int64_t sums[4];
    alignas(32) int64_t diffCounts[4];
    alignas(32) int64_t sqSums[4];
    _mm256_store_si256(reinterpret_cast<__m256i *>(sums), vSumAcc);
    _mm256_store_si256(reinterpret_cast<__m256i *>(diffCounts), vDiffAcc);
    _mm256_store_si256(reinterpret_cast<__m256i *>(sqSums), vSumSqAcc);

    sum += sums[0] + sums[1] + sums[2] + sums[3];
    diffCount += diffCounts[0] + diffCounts[1] + diffCounts[2] + diffCounts[3];
    sumSq += sqSums[0] + sqSums[1] + sqSums[2] + sqSums[3];

    __m128i max128 =
        _mm_max_epu8(_mm256_castsi256_si128(vMaxAcc), _mm256_extracti128_si256(vMaxAcc, 1));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 8));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 4));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 2));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 1));
    maxV = std::max(maxV, static_cast<int>(_mm_extract_epi16(max128, 0) & 0xFF));

    for (; i < count; ++i)
    {
        const int val = data[i];
        sum += val;
        diffCount += (val >= minDiff ? 1 : 0);
        maxV = std::max(maxV, val);
        sumSq += static_cast<int64_t>(val) * val;
    }
}
#endif

static inline void accumulateGrayscaleStatsSSE2(const uint8_t *data, size_t count, int minDiff,
                                                int64_t &sum, int64_t &diffCount, int &maxV,
                                                int64_t &sumSq)
{
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vMinDiff = _mm_set1_epi8(static_cast<char>(minDiff));
    const __m128i vOnes = _mm_set1_epi8(1);
    __m128i vSumAcc = _mm_setzero_si128();
    __m128i vDiffAcc = _mm_setzero_si128();
    __m128i vMaxAcc = _mm_setzero_si128();
    __m128i vSumSqAcc = _mm_setzero_si128();

    size_t i = 0;
    for (; i + 16 <= count; i += 16)
    {
        const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i));
        vMaxAcc = _mm_max_epu8(vMaxAcc, v);

        const __m128i sad = _mm_sad_epu8(v, vzero);
        vSumAcc = _mm_add_epi64(vSumAcc, sad);

        const __m128i under = _mm_subs_epu8(vMinDiff, v);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero);
        const __m128i matchedOnes = _mm_and_si128(mask, vOnes);
        const __m128i diffSad = _mm_sad_epu8(matchedOnes, vzero);
        vDiffAcc = _mm_add_epi64(vDiffAcc, diffSad);

        const __m128i vlo = _mm_unpacklo_epi8(v, vzero);
        const __m128i vhi = _mm_unpackhi_epi8(v, vzero);
        const __m128i sqlo = _mm_madd_epi16(vlo, vlo);
        const __m128i sqhi = _mm_madd_epi16(vhi, vhi);
        vSumSqAcc = _mm_add_epi64(vSumSqAcc, _mm_unpacklo_epi32(sqlo, vzero));
        vSumSqAcc = _mm_add_epi64(vSumSqAcc, _mm_unpackhi_epi32(sqlo, vzero));
        vSumSqAcc = _mm_add_epi64(vSumSqAcc, _mm_unpacklo_epi32(sqhi, vzero));
        vSumSqAcc = _mm_add_epi64(vSumSqAcc, _mm_unpackhi_epi32(sqhi, vzero));
    }

    alignas(16) int64_t sums[2];
    alignas(16) int64_t diffCounts[2];
    alignas(16) int64_t sqSums[2];
    _mm_store_si128(reinterpret_cast<__m128i *>(sums), vSumAcc);
    _mm_store_si128(reinterpret_cast<__m128i *>(diffCounts), vDiffAcc);
    _mm_store_si128(reinterpret_cast<__m128i *>(sqSums), vSumSqAcc);

    sum += sums[0] + sums[1];
    diffCount += diffCounts[0] + diffCounts[1];
    sumSq += sqSums[0] + sqSums[1];

    __m128i max128 = vMaxAcc;
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 8));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 4));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 2));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 1));
    maxV = std::max(maxV, static_cast<int>(_mm_extract_epi16(max128, 0) & 0xFF));

    for (; i < count; ++i)
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
#if defined(__AVX2__) || defined(_M_AVX2)
    const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();
#endif

    if (contiguous)
    {
        const size_t total = static_cast<size_t>(grayDiff.width) * grayDiff.height;
        const uint8_t *src = grayDiff.buffer->data();
#if defined(__AVX2__) || defined(_M_AVX2)
        if (useAvx2)
            accumulateGrayscaleStatsAVX2(src, total, minDiff, sum, diffCount, maxV, sumSq);
        else
#endif
            accumulateGrayscaleStatsSSE2(src, total, minDiff, sum, diffCount, maxV, sumSq);
    }
    else if (cpp == 1 && ro == 0)
    {
        const size_t rowLen = static_cast<size_t>(x1 - x0);
        for (int64_t y = y0; y < y1; ++y)
        {
            const uint8_t *src = grayDiff.buffer->data() +
                                 static_cast<size_t>(y) * grayDiff.stride() +
                                 static_cast<size_t>(x0);
#if defined(__AVX2__) || defined(_M_AVX2)
            if (useAvx2)
                accumulateGrayscaleStatsAVX2(src, rowLen, minDiff, sum, diffCount, maxV, sumSq);
            else
#endif
                accumulateGrayscaleStatsSSE2(src, rowLen, minDiff, sum, diffCount, maxV, sumSq);
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

namespace
{
struct HeatRGB
{
    uint8_t r, g, b;
};
static_assert(sizeof(HeatRGB) == 3, "HeatRGB must be packed 3 bytes");

const auto &heatLUT()
{
    static const auto table = []()
    {
        std::array<HeatRGB, 256> lut{};
        for (int v = 0; v < 256; ++v)
        {
            const uint8_t r = (v < 128) ? 0 : static_cast<uint8_t>((v - 128) * 2);
            const uint8_t g =
                (v < 128) ? static_cast<uint8_t>(v * 2) : static_cast<uint8_t>(255 - (v - 128) * 2);
            const uint8_t b = (v < 128) ? static_cast<uint8_t>(255 - v * 2) : 0;
            lut[v] = HeatRGB{r, g, b};
        }
        return lut;
    }();
    return table;
}

const auto &highlightColorLUT()
{
    static const auto table = []()
    {
        std::array<std::array<uint8_t, 3>, 256> lut{};
        for (int v = 0; v < 256; ++v)
        {
            const uint8_t r = static_cast<uint8_t>(std::min(255, 80 + v * 2));
            uint8_t g = 0;
            uint8_t b = 0;
            if (v < 60)
            {
                // Subtle difference: warm amber warning gradient, strictly R > G > B
                g = static_cast<uint8_t>(std::min<int>(r / 3, 48 - (v * 48 / 60)));
            }
            else if (v >= 180)
            {
                // Severe difference: vivid crimson alert
                b = static_cast<uint8_t>(std::min<int>(24, (v - 180) / 4));
            }
            lut[v] = {r, g, b};
        }
        return lut;
    }();
    return table;
}
} // namespace

ImageData DifferenceEngine::heatMap(const ImageData &gray)
{
    return visualizeOverlay(gray, ImageData(), 0, 1.0, false);
}

ImageData DifferenceEngine::highlightMap(const ImageData &grayDiff, const ImageData &base,
                                         uint8_t threshold)
{
    return visualizeOverlay(grayDiff, base, threshold, 1.0, true);
}

static ImageData renderHeatOverlay(const ImageData &grayDiff, const uint8_t *valLut, int w, int h,
                                   int cppD, int roD, bool isDirectDiff)
{
    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();
    const auto &lut = heatLUT();
    if (isDirectDiff)
    {
        if (grayDiff.stride() == static_cast<ptrdiff_t>(w) &&
            out.stride() == static_cast<ptrdiff_t>(w) * 3)
        {
            const size_t total = static_cast<size_t>(w) * h;
            const uint8_t *src = grayDiff.buffer->data();
            auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data());
            for (size_t i = 0; i < total; ++i)
                dst[i] = lut[valLut[src[i]]];
        }
        else
        {
            for (int y = 0; y < h; ++y)
            {
                const uint8_t *src =
                    grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
                auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data() +
                                                        static_cast<size_t>(y) * out.stride());
                for (int x = 0; x < w; ++x)
                    dst[x] = lut[valLut[src[x]]];
            }
        }
        return out;
    }
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
        auto *dst =
            reinterpret_cast<HeatRGB *>(out.buffer->data() + static_cast<size_t>(y) * out.stride());
        for (int x = 0; x < w; ++x)
            dst[x] = lut[valLut[src[x * cppD + roD]]];
    }
    return out;
}

static ImageData renderHighlightOverlay(const ImageData &grayDiff, const ImageData &base,
                                         const uint8_t *valLut, uint8_t threshold, int w, int h,
                                         int cppD, int roD, bool isDirectDiff)
{
    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();
    const bool hasBase =
        !base.isNull() && base.width >= w && base.height >= h &&
        (base.format == PixelFormat::RGB24 || base.format == PixelFormat::BGR24 ||
         base.format == PixelFormat::RGBA32 || base.format == PixelFormat::BGRA32 ||
         base.format == PixelFormat::Grayscale8);
    const int cppB = hasBase ? base.channelsPerPixel() : 0;
    const int minDiff = std::max<int>(threshold, 1);
    const auto &hlColorLUT = highlightColorLUT();

    if (!hasBase)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t rawV = isDirectDiff ? src[x] : src[x * cppD + roD];
                const uint8_t v = valLut[rawV];
                if (v >= minDiff)
                {
                    const auto &c = hlColorLUT[v];
                    dst[x * 3 + 0] = c[0];
                    dst[x * 3 + 1] = c[1];
                    dst[x * 3 + 2] = c[2];
                }
                else
                {
                    dst[x * 3 + 0] = 128;
                    dst[x * 3 + 1] = 128;
                    dst[x * 3 + 2] = 128;
                }
            }
        }
        return out;
    }

    if (base.format == PixelFormat::Grayscale8)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            const uint8_t *bs = base.buffer->data() + static_cast<size_t>(y) * base.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t rawV = isDirectDiff ? src[x] : src[x * cppD + roD];
                const uint8_t v = valLut[rawV];
                if (v >= minDiff)
                {
                    const auto &c = hlColorLUT[v];
                    dst[x * 3 + 0] = c[0];
                    dst[x * 3 + 1] = c[1];
                    dst[x * 3 + 2] = c[2];
                }
                else
                {
                    const uint8_t g = bs[x];
                    dst[x * 3 + 0] = g;
                    dst[x * 3 + 1] = g;
                    dst[x * 3 + 2] = g;
                }
            }
        }
        return out;
    }

    const bool isBGR = (base.format == PixelFormat::BGR24 || base.format == PixelFormat::BGRA32);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
        const uint8_t *bs = base.buffer->data() + static_cast<size_t>(y) * base.stride();
        uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
        const uint8_t *bp = bs;
        for (int x = 0; x < w; ++x, dst += 3, bp += cppB)
        {
            const uint8_t rawV = isDirectDiff ? src[x] : src[x * cppD + roD];
            const uint8_t v = valLut[rawV];
            if (v >= minDiff)
            {
                const auto &c = hlColorLUT[v];
                dst[0] = c[0];
                dst[1] = c[1];
                dst[2] = c[2];
            }
            else
            {
                const int r = isBGR ? bp[2] : bp[0];
                const int g = bp[1];
                const int b = isBGR ? bp[0] : bp[2];
                const uint8_t gray = static_cast<uint8_t>((r * 30 + g * 59 + b * 11) / 100);
                dst[0] = gray;
                dst[1] = gray;
                dst[2] = gray;
            }
        }
    }
    return out;
}

ImageData DifferenceEngine::visualizeOverlay(const ImageData &grayDiff, const ImageData &base,
                                             uint8_t threshold, double gain, bool highlight)
{
    if (grayDiff.isNull())
        return ImageData();

    alignas(16) uint8_t valLut[256];
    for (int i = 0; i < 256; ++i)
    {
        int v = i;
        if (gain > 1.0)
            v = std::min(255, static_cast<int>(std::round(i * gain)));
        valLut[i] = (v >= threshold) ? static_cast<uint8_t>(v) : 0;
    }

    const int w = grayDiff.width;
    const int h = grayDiff.height;
    const int cppD = grayDiff.channelsPerPixel();
    const int roD = channelOffset(grayDiff.format, 0);
    const bool isDirectDiff = (cppD == 1 && roD == 0);

    if (!highlight)
        return renderHeatOverlay(grayDiff, valLut, w, h, cppD, roD, isDirectDiff);
    return renderHighlightOverlay(grayDiff, base, valLut, threshold, w, h, cppD, roD, isDirectDiff);
}
