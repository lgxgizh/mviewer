#include "core/analysis/AnalysisEngine.h"

#include "core/image/QtConvert.h"
#include "core/simd/CpuFeatures.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>
#include <vector>

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
inline void accumulateSsdBytesAVX2(const uint8_t *a, const uint8_t *b, size_t count, int64_t &sumSq)
{
    const __m256i vzero = _mm256_setzero_si256();
    __m256i vAcc64 = _mm256_setzero_si256();
    __m256i vAcc32 = _mm256_setzero_si256();

    size_t i = 0;
    size_t chunkCount = 0;
    for (; i + 32 <= count; i += 32)
    {
        const __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(a + i));
        const __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(b + i));

        const __m256i vlo_a = _mm256_unpacklo_epi8(va, vzero);
        const __m256i vlo_b = _mm256_unpacklo_epi8(vb, vzero);
        const __m256i diff_lo = _mm256_sub_epi16(vlo_a, vlo_b);
        const __m256i sq_lo = _mm256_madd_epi16(diff_lo, diff_lo);

        const __m256i vhi_a = _mm256_unpackhi_epi8(va, vzero);
        const __m256i vhi_b = _mm256_unpackhi_epi8(vb, vzero);
        const __m256i diff_hi = _mm256_sub_epi16(vhi_a, vhi_b);
        const __m256i sq_hi = _mm256_madd_epi16(diff_hi, diff_hi);

        vAcc32 = _mm256_add_epi32(vAcc32, _mm256_add_epi32(sq_lo, sq_hi));

        if (++chunkCount >= 16384)
        {
            const __m128i lo128 = _mm256_castsi256_si128(vAcc32);
            const __m128i hi128 = _mm256_extracti128_si256(vAcc32, 1);
            vAcc64 = _mm256_add_epi64(
                vAcc64, _mm256_set_m128i(_mm_unpackhi_epi32(hi128, _mm_setzero_si128()),
                                         _mm_unpacklo_epi32(hi128, _mm_setzero_si128())));
            vAcc64 = _mm256_add_epi64(
                vAcc64, _mm256_set_m128i(_mm_unpackhi_epi32(lo128, _mm_setzero_si128()),
                                         _mm_unpacklo_epi32(lo128, _mm_setzero_si128())));
            vAcc32 = _mm256_setzero_si256();
            chunkCount = 0;
        }
    }

    const __m128i lo128 = _mm256_castsi256_si128(vAcc32);
    const __m128i hi128 = _mm256_extracti128_si256(vAcc32, 1);
    vAcc64 =
        _mm256_add_epi64(vAcc64, _mm256_set_m128i(_mm_unpackhi_epi32(hi128, _mm_setzero_si128()),
                                                  _mm_unpacklo_epi32(hi128, _mm_setzero_si128())));
    vAcc64 =
        _mm256_add_epi64(vAcc64, _mm256_set_m128i(_mm_unpackhi_epi32(lo128, _mm_setzero_si128()),
                                                  _mm_unpacklo_epi32(lo128, _mm_setzero_si128())));

    alignas(32) int64_t sums[4];
    _mm256_store_si256(reinterpret_cast<__m256i *>(sums), vAcc64);
    sumSq += sums[0] + sums[1] + sums[2] + sums[3];

    for (; i < count; ++i)
    {
        const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        sumSq += static_cast<int64_t>(d) * d;
    }
}

inline void accumulateSsdBytesSSE2(const uint8_t *a, const uint8_t *b, size_t count, int64_t &sumSq)
{
    const __m128i vzero = _mm_setzero_si128();
    __m128i vAcc64 = _mm_setzero_si128();
    __m128i vAcc32 = _mm_setzero_si128();

    size_t i = 0;
    size_t chunkCount = 0;
    for (; i + 16 <= count; i += 16)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + i));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + i));

        const __m128i vlo_a = _mm_unpacklo_epi8(va, vzero);
        const __m128i vlo_b = _mm_unpacklo_epi8(vb, vzero);
        const __m128i diff_lo = _mm_sub_epi16(vlo_a, vlo_b);
        const __m128i sq_lo = _mm_madd_epi16(diff_lo, diff_lo);

        const __m128i vhi_a = _mm_unpackhi_epi8(va, vzero);
        const __m128i vhi_b = _mm_unpackhi_epi8(vb, vzero);
        const __m128i diff_hi = _mm_sub_epi16(vhi_a, vhi_b);
        const __m128i sq_hi = _mm_madd_epi16(diff_hi, diff_hi);

        vAcc32 = _mm_add_epi32(vAcc32, _mm_add_epi32(sq_lo, sq_hi));

        if (++chunkCount >= 16384)
        {
            vAcc64 = _mm_add_epi64(vAcc64, _mm_unpacklo_epi32(vAcc32, vzero));
            vAcc64 = _mm_add_epi64(vAcc64, _mm_unpackhi_epi32(vAcc32, vzero));
            vAcc32 = _mm_setzero_si128();
            chunkCount = 0;
        }
    }

    vAcc64 = _mm_add_epi64(vAcc64, _mm_unpacklo_epi32(vAcc32, vzero));
    vAcc64 = _mm_add_epi64(vAcc64, _mm_unpackhi_epi32(vAcc32, vzero));

    alignas(16) int64_t sums[2];
    _mm_store_si128(reinterpret_cast<__m128i *>(sums), vAcc64);
    sumSq += sums[0] + sums[1];

    for (; i < count; ++i)
    {
        const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        sumSq += static_cast<int64_t>(d) * d;
    }
}

inline void accumulateSsdRgbaSSE2(const uint8_t *a, const uint8_t *b, size_t pixelCount,
                                  int64_t &sumSq)
{
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vRgbMask16 = _mm_setr_epi16(-1, -1, -1, 0, -1, -1, -1, 0);
    __m128i vAcc64 = _mm_setzero_si128();
    __m128i vAcc32 = _mm_setzero_si128();

    size_t i = 0;
    size_t chunkCount = 0;
    for (; i + 4 <= pixelCount; i += 4)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + i * 4));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + i * 4));

        __m128i diff_lo = _mm_sub_epi16(_mm_unpacklo_epi8(va, vzero), _mm_unpacklo_epi8(vb, vzero));
        diff_lo = _mm_and_si128(diff_lo, vRgbMask16);
        const __m128i sq_lo = _mm_madd_epi16(diff_lo, diff_lo);

        __m128i diff_hi = _mm_sub_epi16(_mm_unpackhi_epi8(va, vzero), _mm_unpackhi_epi8(vb, vzero));
        diff_hi = _mm_and_si128(diff_hi, vRgbMask16);
        const __m128i sq_hi = _mm_madd_epi16(diff_hi, diff_hi);

        vAcc32 = _mm_add_epi32(vAcc32, _mm_add_epi32(sq_lo, sq_hi));

        if (++chunkCount >= 16384)
        {
            vAcc64 = _mm_add_epi64(vAcc64, _mm_unpacklo_epi32(vAcc32, vzero));
            vAcc64 = _mm_add_epi64(vAcc64, _mm_unpackhi_epi32(vAcc32, vzero));
            vAcc32 = _mm_setzero_si128();
            chunkCount = 0;
        }
    }

    vAcc64 = _mm_add_epi64(vAcc64, _mm_unpacklo_epi32(vAcc32, vzero));
    vAcc64 = _mm_add_epi64(vAcc64, _mm_unpackhi_epi32(vAcc32, vzero));

    alignas(16) int64_t sums[2];
    _mm_store_si128(reinterpret_cast<__m128i *>(sums), vAcc64);
    sumSq += sums[0] + sums[1];

    for (; i < pixelCount; ++i)
    {
        const int dr = static_cast<int>(a[i * 4 + 0]) - static_cast<int>(b[i * 4 + 0]);
        const int dg = static_cast<int>(a[i * 4 + 1]) - static_cast<int>(b[i * 4 + 1]);
        const int db = static_cast<int>(a[i * 4 + 2]) - static_cast<int>(b[i * 4 + 2]);
        sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                 static_cast<int64_t>(db) * db;
    }
}

inline void computeBlockStatsSSE2(const uint8_t *const linesA[8], const uint8_t *const linesB[8],
                                  int bx, int &sumA, int &sumB, int &sumAA, int &sumBB, int &sumAB)
{
    const __m128i vzero = _mm_setzero_si128();
    __m128i vSumAA = _mm_setzero_si128();
    __m128i vSumBB = _mm_setzero_si128();
    __m128i vSumAB = _mm_setzero_si128();
    int sA = 0, sB = 0;

    for (int y = 0; y < 8; ++y)
    {
        const uint8_t *la = linesA[y] + bx;
        const uint8_t *lb = linesB[y] + bx;

        const __m128i a8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(la));
        const __m128i b8 = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(lb));

        const __m128i sadA = _mm_sad_epu8(a8, vzero);
        const __m128i sadB = _mm_sad_epu8(b8, vzero);
        sA += _mm_cvtsi128_si32(sadA);
        sB += _mm_cvtsi128_si32(sadB);

        const __m128i a16 = _mm_unpacklo_epi8(a8, vzero);
        const __m128i b16 = _mm_unpacklo_epi8(b8, vzero);

        vSumAA = _mm_add_epi32(vSumAA, _mm_madd_epi16(a16, a16));
        vSumBB = _mm_add_epi32(vSumBB, _mm_madd_epi16(b16, b16));
        vSumAB = _mm_add_epi32(vSumAB, _mm_madd_epi16(a16, b16));
    }

    sumA = sA;
    sumB = sB;

    alignas(16) int32_t arrAA[4], arrBB[4], arrAB[4];
    _mm_store_si128(reinterpret_cast<__m128i *>(arrAA), vSumAA);
    _mm_store_si128(reinterpret_cast<__m128i *>(arrBB), vSumBB);
    _mm_store_si128(reinterpret_cast<__m128i *>(arrAB), vSumAB);

    sumAA = arrAA[0] + arrAA[1] + arrAA[2] + arrAA[3];
    sumBB = arrBB[0] + arrBB[1] + arrBB[2] + arrBB[3];
    sumAB = arrAB[0] + arrAB[1] + arrAB[2] + arrAB[3];
}

template <typename LineGetterA, typename LineGetterB>
double computeSSIMCore(int w, int h, LineGetterA &&lineA, LineGetterB &&lineB)
{
    const double C1 = (0.01 * 255.0) * (0.01 * 255.0);
    const double C2 = (0.03 * 255.0) * (0.03 * 255.0);
    constexpr int block = 8;
    constexpr double N = 64.0;

    double ssimSum = 0.0;
    int blocks = 0;
    for (int by = 0; by + block <= h; by += block)
    {
        const uint8_t *linesA[block];
        const uint8_t *linesB[block];
        for (int y = 0; y < block; ++y)
        {
            linesA[y] = lineA(by + y);
            linesB[y] = lineB(by + y);
        }

        for (int bx = 0; bx + block <= w; bx += block)
        {
            int sumA = 0, sumB = 0, sumAA = 0, sumBB = 0, sumAB = 0;
            computeBlockStatsSSE2(linesA, linesB, bx, sumA, sumB, sumAA, sumBB, sumAB);

            const double meanA = sumA / N;
            const double meanB = sumB / N;
            const double varA = std::max(0.0, (sumAA / N) - meanA * meanA);
            const double varB = std::max(0.0, (sumBB / N) - meanB * meanB);
            const double cov = (sumAB / N) - meanA * meanB;
            const double num = (2.0 * meanA * meanB + C1) * (2.0 * cov + C2);
            const double den = (meanA * meanA + meanB * meanB + C1) * (varA + varB + C2);
            ssimSum += num / den;
            ++blocks;
        }
    }
    return blocks > 0 ? ssimSum / blocks : 0.0;
}

inline void calcLaplacianRowSSE2(const uint8_t *prev, const uint8_t *curr, const uint8_t *next,
                                 int w, int64_t &sum, int64_t &sumSq)
{
    const __m128i vzero = _mm_setzero_si128();
    const __m128i vOnes = _mm_set1_epi16(1);

    __m128i vSum64 = _mm_setzero_si128();
    __m128i vSumSq64 = _mm_setzero_si128();
    __m128i vSum32 = _mm_setzero_si128();
    __m128i vSumSq32 = _mm_setzero_si128();

    int x = 1;
    int chunk = 0;
    for (; x + 16 <= w - 1; x += 16)
    {
        const __m128i p = _mm_loadu_si128(reinterpret_cast<const __m128i *>(prev + x));
        const __m128i n = _mm_loadu_si128(reinterpret_cast<const __m128i *>(next + x));
        const __m128i c = _mm_loadu_si128(reinterpret_cast<const __m128i *>(curr + x));
        const __m128i cl = _mm_loadu_si128(reinterpret_cast<const __m128i *>(curr + x - 1));
        const __m128i cr = _mm_loadu_si128(reinterpret_cast<const __m128i *>(curr + x + 1));

        const __m128i p_lo = _mm_unpacklo_epi8(p, vzero);
        const __m128i n_lo = _mm_unpacklo_epi8(n, vzero);
        const __m128i c_lo = _mm_unpacklo_epi8(c, vzero);
        const __m128i cl_lo = _mm_unpacklo_epi8(cl, vzero);
        const __m128i cr_lo = _mm_unpacklo_epi8(cr, vzero);

        const __m128i neighbors_lo =
            _mm_add_epi16(_mm_add_epi16(p_lo, n_lo), _mm_add_epi16(cl_lo, cr_lo));
        const __m128i c4_lo = _mm_slli_epi16(c_lo, 2);
        const __m128i lap_lo = _mm_sub_epi16(neighbors_lo, c4_lo);

        const __m128i p_hi = _mm_unpackhi_epi8(p, vzero);
        const __m128i n_hi = _mm_unpackhi_epi8(n, vzero);
        const __m128i c_hi = _mm_unpackhi_epi8(c, vzero);
        const __m128i cl_hi = _mm_unpackhi_epi8(cl, vzero);
        const __m128i cr_hi = _mm_unpackhi_epi8(cr, vzero);

        const __m128i neighbors_hi =
            _mm_add_epi16(_mm_add_epi16(p_hi, n_hi), _mm_add_epi16(cl_hi, cr_hi));
        const __m128i c4_hi = _mm_slli_epi16(c_hi, 2);
        const __m128i lap_hi = _mm_sub_epi16(neighbors_hi, c4_hi);

        vSum32 = _mm_add_epi32(vSum32, _mm_madd_epi16(lap_lo, vOnes));
        vSum32 = _mm_add_epi32(vSum32, _mm_madd_epi16(lap_hi, vOnes));

        vSumSq32 = _mm_add_epi32(vSumSq32, _mm_madd_epi16(lap_lo, lap_lo));
        vSumSq32 = _mm_add_epi32(vSumSq32, _mm_madd_epi16(lap_hi, lap_hi));

        if (++chunk >= 256)
        {
            const __m128i signSum = _mm_srai_epi32(vSum32, 31);
            vSum64 = _mm_add_epi64(vSum64, _mm_unpacklo_epi32(vSum32, signSum));
            vSum64 = _mm_add_epi64(vSum64, _mm_unpackhi_epi32(vSum32, signSum));
            vSum32 = _mm_setzero_si128();

            vSumSq64 = _mm_add_epi64(vSumSq64, _mm_unpacklo_epi32(vSumSq32, vzero));
            vSumSq64 = _mm_add_epi64(vSumSq64, _mm_unpackhi_epi32(vSumSq32, vzero));
            vSumSq32 = _mm_setzero_si128();
            chunk = 0;
        }
    }

    const __m128i signSum = _mm_srai_epi32(vSum32, 31);
    vSum64 = _mm_add_epi64(vSum64, _mm_unpacklo_epi32(vSum32, signSum));
    vSum64 = _mm_add_epi64(vSum64, _mm_unpackhi_epi32(vSum32, signSum));

    vSumSq64 = _mm_add_epi64(vSumSq64, _mm_unpacklo_epi32(vSumSq32, vzero));
    vSumSq64 = _mm_add_epi64(vSumSq64, _mm_unpackhi_epi32(vSumSq32, vzero));

    alignas(16) int64_t arrSum[2], arrSumSq[2];
    _mm_store_si128(reinterpret_cast<__m128i *>(arrSum), vSum64);
    _mm_store_si128(reinterpret_cast<__m128i *>(arrSumSq), vSumSq64);

    sum += arrSum[0] + arrSum[1];
    sumSq += arrSumSq[0] + arrSumSq[1];

    for (; x < w - 1; ++x)
    {
        const int lap = static_cast<int>(prev[x]) + curr[x - 1] + curr[x + 1] + next[x] -
                        4 * static_cast<int>(curr[x]);
        sum += lap;
        sumSq += static_cast<int64_t>(lap) * lap;
    }
}

template <typename LineGetter> double calcLaplacianCore(int w, int h, LineGetter &&getLine)
{
    if (w < 3 || h < 3)
        return 0.0;
    const int count = (w - 2) * (h - 2);
    if (count < 2)
        return 0.0;

    int64_t sum = 0;
    int64_t sumSq = 0;
    const uint8_t *prev = getLine(0);
    const uint8_t *curr = getLine(1);
    for (int y = 1; y < h - 1; ++y)
    {
        const uint8_t *next = getLine(y + 1);
        calcLaplacianRowSSE2(prev, curr, next, w, sum, sumSq);
        prev = curr;
        curr = next;
    }
    const double mean = static_cast<double>(sum) / count;
    const double variance = static_cast<double>(sumSq) / count - mean * mean;
    return std::max(0.0, variance);
}

void accumulateGraySsd(const ImageBuffer &va, const ImageBuffer &vb, int w, int h, bool useAvx2,
                       int64_t &sumSq)
{
    const bool isContiguous = (w == va.width && va.stride() == static_cast<ptrdiff_t>(w) &&
                               vb.stride() == static_cast<ptrdiff_t>(w));
    if (isContiguous)
    {
        const size_t total = static_cast<size_t>(w) * h;
        if (useAvx2)
            accumulateSsdBytesAVX2(va.data, vb.data, total, sumSq);
        else
            accumulateSsdBytesSSE2(va.data, vb.data, total, sumSq);
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            if (useAvx2)
                accumulateSsdBytesAVX2(la, lb, static_cast<size_t>(w), sumSq);
            else
                accumulateSsdBytesSSE2(la, lb, static_cast<size_t>(w), sumSq);
        }
    }
}

void accumulateRgb24Ssd(const ImageBuffer &va, const ImageBuffer &vb, int w, int h, bool useAvx2,
                        int64_t &sumSq)
{
    const bool isContiguous = (w == va.width && va.stride() == static_cast<ptrdiff_t>(w) * 3 &&
                               vb.stride() == static_cast<ptrdiff_t>(w) * 3);
    if (isContiguous)
    {
        const size_t totalBytes = static_cast<size_t>(w) * 3 * static_cast<size_t>(h);
        if (useAvx2)
            accumulateSsdBytesAVX2(va.data, vb.data, totalBytes, sumSq);
        else
            accumulateSsdBytesSSE2(va.data, vb.data, totalBytes, sumSq);
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            if (useAvx2)
                accumulateSsdBytesAVX2(la, lb, static_cast<size_t>(w) * 3, sumSq);
            else
                accumulateSsdBytesSSE2(la, lb, static_cast<size_t>(w) * 3, sumSq);
        }
    }
}

void accumulateRgba32Ssd(const ImageBuffer &va, const ImageBuffer &vb, int w, int h, int64_t &sumSq)
{
    const bool isContiguous = (w == va.width && va.stride() == static_cast<ptrdiff_t>(w) * 4 &&
                               vb.stride() == static_cast<ptrdiff_t>(w) * 4);
    if (isContiguous)
    {
        accumulateSsdRgbaSSE2(va.data, vb.data, static_cast<size_t>(w) * h, sumSq);
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            accumulateSsdRgbaSSE2(la, lb, static_cast<size_t>(w), sumSq);
        }
    }
}

bool accumulateCrossFormatSsd(const ImageData &aData, const ImageData &bData, int w, int h,
                              int64_t &sumSq)
{
    const bool isCrossRgbBgr24 =
        (aData.format == PixelFormat::RGB24 && bData.format == PixelFormat::BGR24) ||
        (aData.format == PixelFormat::BGR24 && bData.format == PixelFormat::RGB24);
    if (isCrossRgbBgr24)
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            for (int x = 0; x < w; ++x)
            {
                const int dr = static_cast<int>(la[x * 3 + 0]) - static_cast<int>(lb[x * 3 + 2]);
                const int dg = static_cast<int>(la[x * 3 + 1]) - static_cast<int>(lb[x * 3 + 1]);
                const int db = static_cast<int>(la[x * 3 + 2]) - static_cast<int>(lb[x * 3 + 0]);
                sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                         static_cast<int64_t>(db) * db;
            }
        }
        return true;
    }

    const bool isCrossRgbaBgra32 =
        (aData.format == PixelFormat::RGBA32 && bData.format == PixelFormat::BGRA32) ||
        (aData.format == PixelFormat::BGRA32 && bData.format == PixelFormat::RGBA32);
    if (isCrossRgbaBgra32)
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
            for (int x = 0; x < w; ++x)
            {
                const int dr = static_cast<int>(la[x * 4 + 0]) - static_cast<int>(lb[x * 4 + 2]);
                const int dg = static_cast<int>(la[x * 4 + 1]) - static_cast<int>(lb[x * 4 + 1]);
                const int db = static_cast<int>(la[x * 4 + 2]) - static_cast<int>(lb[x * 4 + 0]);
                sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                         static_cast<int64_t>(db) * db;
            }
        }
        return true;
    }

    return false;
}

void accumulateGenericSsd(const ImageData &aData, const ImageData &bData, int w, int h,
                          int64_t &sumSq)
{
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_RGB32);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        const QRgb *la = reinterpret_cast<const QRgb *>(aa.constScanLine(y));
        const QRgb *lb = reinterpret_cast<const QRgb *>(bb.constScanLine(y));
        for (int x = 0; x < w; ++x)
        {
            const int dr = static_cast<int>(qRed(la[x])) - qRed(lb[x]);
            const int dg = static_cast<int>(qGreen(la[x])) - qGreen(lb[x]);
            const int db = static_cast<int>(qBlue(la[x])) - qBlue(lb[x]);
            sumSq += static_cast<int64_t>(dr) * dr + static_cast<int64_t>(dg) * dg +
                     static_cast<int64_t>(db) * db;
        }
    }
}

} // namespace

double AnalysisEngine::psnr(const ImageData &aData, const ImageData &bData)
{
    if (aData.isNull() || bData.isNull())
        return 0.0;
    const int w = std::min(aData.width, bData.width);
    const int h = std::min(aData.height, bData.height);
    if (w <= 0 || h <= 0)
        return 0.0;

    int64_t sumSq = 0;
    int channels = 3;
    const bool isSameFormat = (aData.format == bData.format);
    const bool useAvx2 = mviewer::core::CpuFeatures::hasAvx2();

    if (isSameFormat && aData.format == PixelFormat::Grayscale8)
    {
        accumulateGraySsd(aData.view(), bData.view(), w, h, useAvx2, sumSq);
        channels = 1;
    }
    else if (isSameFormat &&
             (aData.format == PixelFormat::RGB24 || aData.format == PixelFormat::BGR24))
    {
        accumulateRgb24Ssd(aData.view(), bData.view(), w, h, useAvx2, sumSq);
        channels = 3;
    }
    else if (isSameFormat &&
             (aData.format == PixelFormat::RGBA32 || aData.format == PixelFormat::BGRA32))
    {
        accumulateRgba32Ssd(aData.view(), bData.view(), w, h, sumSq);
        channels = 3;
    }
    else if (accumulateCrossFormatSsd(aData, bData, w, h, sumSq))
    {
        channels = 3;
    }
    else
    {
        accumulateGenericSsd(aData, bData, w, h, sumSq);
        channels = 3;
    }

    const double mse = static_cast<double>(sumSq) / (1.0 * w * h * channels);
    if (mse <= 1e-10)
        return 100.0; // 完美一致(而非 inf)
    return 10.0 * std::log10(65025.0 / mse);
}

double AnalysisEngine::ssim(const ImageData &aData, const ImageData &bData)
{
    if (aData.isNull() || bData.isNull())
        return 0.0;
    const int w = std::min(aData.width, bData.width);
    const int h = std::min(aData.height, bData.height);
    if (w < 8 || h < 8)
        return 0.0;

    if (aData.format == PixelFormat::Grayscale8 && bData.format == PixelFormat::Grayscale8)
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        return computeSSIMCore(
            w, h, [&va](int y) { return va.data + static_cast<size_t>(y) * va.stride(); },
            [&vb](int y) { return vb.data + static_cast<size_t>(y) * vb.stride(); });
    }

    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_Grayscale8);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_Grayscale8);
    return computeSSIMCore(
        w, h, [&aa](int y) { return aa.constScanLine(y); },
        [&bb](int y) { return bb.constScanLine(y); });
}

double AnalysisEngine::noiseEstimate(const ImageData &imgData)
{
    if (imgData.isNull())
        return 0.0;
    const int w = imgData.width;
    const int h = imgData.height;
    if (w < 3 || h < 3)
        return 0.0;

    if (imgData.format == PixelFormat::Grayscale8)
    {
        const ImageBuffer v = imgData.view();
        return calcLaplacianCore(w, h, [&v](int y)
                                 { return v.data + static_cast<size_t>(y) * v.stride(); });
    }

    QImage img = mvcore::toQImage(imgData).convertToFormat(QImage::Format_Grayscale8);
    return calcLaplacianCore(w, h, [&img](int y) { return img.constScanLine(y); });
}
