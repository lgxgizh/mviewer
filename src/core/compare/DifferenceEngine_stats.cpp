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

MV_TARGET_AVX2
static inline void accumulateGrayscaleStatsAVX2(const uint8_t *data, size_t count, int minDiff,
                                                long long &sum, long long &diffCount, int &maxV)
{
    const __m256i vzero = _mm256_setzero_si256();
    const __m256i vMinDiff = _mm256_set1_epi8(static_cast<char>(minDiff));
    const __m256i vOnes = _mm256_set1_epi8(1);
    __m256i vSumAcc = _mm256_setzero_si256();
    __m256i vDiffAcc = _mm256_setzero_si256();
    __m256i vMaxAcc = _mm256_setzero_si256();

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
    }

    alignas(32) int64_t sums[4];
    alignas(32) int64_t diffCounts[4];
    _mm256_store_si256(reinterpret_cast<__m256i *>(sums), vSumAcc);
    _mm256_store_si256(reinterpret_cast<__m256i *>(diffCounts), vDiffAcc);

    sum += sums[0] + sums[1] + sums[2] + sums[3];
    diffCount += diffCounts[0] + diffCounts[1] + diffCounts[2] + diffCounts[3];

    __m128i max128 =
        _mm_max_epu8(_mm256_castsi256_si128(vMaxAcc), _mm256_extracti128_si256(vMaxAcc, 1));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 8));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 4));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 2));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 1));
    maxV = std::max(maxV, _mm_extract_epi16(max128, 0) & 0xFF);

    for (; i < count; ++i)
    {
        const int v = data[i];
        sum += v;
        diffCount += (v >= minDiff);
        maxV = std::max(maxV, v);
    }
}

static inline void accumulateGrayscaleStatsSSE2(const uint8_t *data, size_t count, int minDiff,
                                                long long &sum, long long &diffCount, int &maxV)
{
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vMinDiff = _mm_set1_epi8(static_cast<char>(minDiff));
    const __m128i vOnes = _mm_set1_epi8(1);
    __m128i vSumAcc = _mm_setzero_si128();
    __m128i vDiffAcc = _mm_setzero_si128();
    __m128i vMaxAcc = _mm_setzero_si128();

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
    }

    alignas(16) int64_t sums[2];
    alignas(16) int64_t diffCounts[2];
    _mm_store_si128(reinterpret_cast<__m128i *>(sums), vSumAcc);
    _mm_store_si128(reinterpret_cast<__m128i *>(diffCounts), vDiffAcc);

    sum += sums[0] + sums[1];
    diffCount += diffCounts[0] + diffCounts[1];

    __m128i max128 = vMaxAcc;
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 8));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 4));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 2));
    max128 = _mm_max_epu8(max128, _mm_srli_si128(max128, 1));
    maxV = std::max(maxV, _mm_extract_epi16(max128, 0) & 0xFF);

    for (; i < count; ++i)
    {
        const int v = data[i];
        sum += v;
        diffCount += (v >= minDiff);
        maxV = std::max(maxV, v);
    }
}

DifferenceEngine::DiffStats DifferenceEngine::computeStats(const ImageData &grayDiff,
                                                           uint8_t threshold)
{
    if (grayDiff.isNull())
        return DiffStats{};
    return computeStats(grayDiff, threshold, 0, 0, grayDiff.width, grayDiff.height);
}

DifferenceEngine::DiffStats DifferenceEngine::computeStats(const ImageData &grayDiff,
                                                           uint8_t threshold, int roiX, int roiY,
                                                           int roiW, int roiH)
{
    DiffStats s;
    if (grayDiff.isNull() || roiW <= 0 || roiH <= 0)
        return s;

    // 64-bit coordinate clamping against integer overflow
    const long long x0ll = std::clamp<long long>(roiX, 0, grayDiff.width);
    const long long y0ll = std::clamp<long long>(roiY, 0, grayDiff.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(roiX) + roiW, 0, grayDiff.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(roiY) + roiH, 0, grayDiff.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x0 >= x1 || y0 >= y1)
        return s;

    const int cpp = grayDiff.channelsPerPixel();
    const int ro = channelOffset(grayDiff.format, 0);
    const int minDiff = std::max<int>(threshold, 1);

    long long sum = 0;
    long long diffCount = 0;
    int maxV = 0;

    const bool contiguous =
        (cpp == 1 && ro == 0 && x0 == 0 && y0 == 0 && x1 == grayDiff.width &&
         y1 == grayDiff.height && grayDiff.stride() == static_cast<size_t>(grayDiff.width));

    const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();

    if (contiguous)
    {
        const size_t total = static_cast<size_t>(grayDiff.width) * grayDiff.height;
        const uint8_t *src = grayDiff.buffer->data();
        if (useAvx2)
            accumulateGrayscaleStatsAVX2(src, total, minDiff, sum, diffCount, maxV);
        else
            accumulateGrayscaleStatsSSE2(src, total, minDiff, sum, diffCount, maxV);
    }
    else if (cpp == 1 && ro == 0)
    {
        const size_t rowLen = static_cast<size_t>(x1 - x0);
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride() + x0;
            if (useAvx2)
                accumulateGrayscaleStatsAVX2(src, rowLen, minDiff, sum, diffCount, maxV);
            else
                accumulateGrayscaleStatsSSE2(src, rowLen, minDiff, sum, diffCount, maxV);
        }
    }
    else
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *src = grayDiff.buffer->data() +
                                 static_cast<size_t>(y) * grayDiff.stride() +
                                 static_cast<size_t>(x0) * cpp + ro;
            for (int x = x0; x < x1; ++x, src += cpp)
            {
                const int v = *src;
                sum += v;
                diffCount += (v >= minDiff);
                maxV = std::max(maxV, v);
            }
        }
    }

    const long long count = static_cast<long long>(x1 - x0) * (y1 - y0);
    s.totalPixels = count;
    s.diffPixels = diffCount;
    s.diffRatio = count > 0 ? static_cast<double>(diffCount) / static_cast<double>(count) : 0.0;
    s.meanDiff = count > 0 ? static_cast<double>(sum) / static_cast<double>(count) : 0.0;
    s.maxDiff = maxV;
    return s;
}
