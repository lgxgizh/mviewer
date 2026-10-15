// Included by DifferenceEngine.cpp to handle AVX2/SSE2 grayscale statistics
// accumulation without exceeding single-file line caps.

#if defined(MV_ARCH_X86)
MV_TARGET_AVX2
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

MV_TARGET_SSE2
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
#endif
