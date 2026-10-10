#include "core/render/DisplayResampleDetail.h"

#include "core/simd/CpuFeatures.h"

#include <cstdint>
#include <cstring>
#include <vector>

#if defined(__GNUC__) || defined(__clang__)
#define MV_TARGET_AVX2 __attribute__((target("avx2")))
#define MV_TARGET_SSE41 __attribute__((target("sse4.1")))
#else
#define MV_TARGET_AVX2
#define MV_TARGET_SSE41
#endif

#if defined(_MSC_VER)
#define MV_NOINLINE __declspec(noinline)
#else
#define MV_NOINLINE
#endif

namespace mviewer::core::resample_detail
{
namespace
{

enum class PairIsa : std::uint8_t
{
    Sse,
    Avx
};

struct BoxScratch
{
    std::vector<uint8_t> rawR;
    std::vector<uint8_t> rawG;
    std::vector<uint8_t> rawB;
    std::vector<uint8_t> outR;
    std::vector<uint8_t> outG;
    std::vector<uint8_t> outB;
};

struct HorizScratch
{
    std::vector<uint8_t> r;
    std::vector<uint8_t> g;
    std::vector<uint8_t> b;
};

BoxScratch &boxScratch()
{
    static thread_local BoxScratch scratch;
    return scratch;
}

HorizScratch &horizScratch()
{
    static thread_local HorizScratch scratch;
    return scratch;
}

void fitBytes(std::vector<uint8_t> &dst, int count)
{
    if (count > 0 && static_cast<int>(dst.size()) < count)
        dst.resize(static_cast<size_t>(count));
}

const uint8_t *packedRow(const RgbView &src, int y)
{
    return src.data + static_cast<size_t>(y) * static_cast<size_t>(src.stride) *
                          static_cast<size_t>(src.channels);
}

void deinterleave(const uint8_t *row, int width, uint8_t *r, uint8_t *g, uint8_t *b)
{
    for (int x = 0; x < width; ++x)
    {
        const uint8_t *pixel = row + static_cast<size_t>(x) * 3u;
        r[x] = pixel[0];
        g[x] = pixel[1];
        b[x] = pixel[2];
    }
}

void averagePairsScalar(const uint8_t *src, uint8_t *dst, int pairs)
{
    for (int i = 0; i < pairs; ++i)
    {
        const int a = src[static_cast<size_t>(i) * 2u];
        const int b = src[static_cast<size_t>(i) * 2u + 1u];
        dst[i] = static_cast<uint8_t>((a + b + 1) >> 1);
    }
}

void averageBytesScalar(const uint8_t *a, const uint8_t *b, uint8_t *dst, int count)
{
    for (int i = 0; i < count; ++i)
        dst[i] = static_cast<uint8_t>((static_cast<int>(a[i]) + static_cast<int>(b[i]) + 1) >> 1);
}

MV_TARGET_AVX2 MV_NOINLINE void averagePairsAvx2(const uint8_t *src, uint8_t *dst, int pairs)
{
    const __m256i one = _mm256_set1_epi16(1);
    const __m256i mask = _mm256_set1_epi16(static_cast<short>(255));
    int i = 0;
    for (; i + 16 <= pairs; i += 16)
    {
        const __m256i v = _mm256_loadu_si256(
            reinterpret_cast<const __m256i *>(src + static_cast<size_t>(i) * 2u));
        const __m256i lo = _mm256_and_si256(v, mask);
        const __m256i hi = _mm256_srli_epi16(v, 8);
        __m256i sum = _mm256_add_epi16(lo, hi);
        sum = _mm256_add_epi16(sum, one);
        const __m256i avg = _mm256_srli_epi16(sum, 1);
        const __m128i packed =
            _mm_packus_epi16(_mm256_castsi256_si128(avg), _mm256_extracti128_si256(avg, 1));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), packed);
    }
    averagePairsScalar(src + static_cast<size_t>(i) * 2u, dst + i, pairs - i);
}

MV_TARGET_SSE41 MV_NOINLINE void averagePairsSse(const uint8_t *src, uint8_t *dst, int pairs)
{
    const __m128i one = _mm_set1_epi16(1);
    const __m128i mask = _mm_set1_epi16(static_cast<short>(255));
    int i = 0;
    for (; i + 8 <= pairs; i += 8)
    {
        const __m128i v =
            _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + static_cast<size_t>(i) * 2u));
        const __m128i lo = _mm_and_si128(v, mask);
        const __m128i hi = _mm_srli_epi16(v, 8);
        __m128i sum = _mm_add_epi16(lo, hi);
        sum = _mm_add_epi16(sum, one);
        const __m128i avg = _mm_srli_epi16(sum, 1);
        const __m128i packed = _mm_packus_epi16(avg, _mm_setzero_si128());
        _mm_storel_epi64(reinterpret_cast<__m128i *>(dst + i), packed);
    }
    averagePairsScalar(src + static_cast<size_t>(i) * 2u, dst + i, pairs - i);
}

MV_TARGET_AVX2 MV_NOINLINE void averageBytesAvx2(const uint8_t *a, const uint8_t *b, uint8_t *dst,
                                                 int count)
{
    int i = 0;
    for (; i + 32 <= count; i += 32)
    {
        const __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(a + i));
        const __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(b + i));
        const __m256i avg = _mm256_avg_epu8(va, vb);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + i), avg);
    }
    averageBytesScalar(a + i, b + i, dst + i, count - i);
}

MV_TARGET_SSE41 MV_NOINLINE void averageBytesSse(const uint8_t *a, const uint8_t *b, uint8_t *dst,
                                                 int count)
{
    int i = 0;
    for (; i + 16 <= count; i += 16)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + i));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + i));
        const __m128i avg = _mm_avg_epu8(va, vb);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + i), avg);
    }
    averageBytesScalar(a + i, b + i, dst + i, count - i);
}

void reducePlanar(const uint8_t *src, uint8_t *dst, int bodyW, bool halfX, PairIsa isa)
{
    if (!halfX)
    {
        if (src != dst)
            std::memcpy(dst, src, static_cast<size_t>(bodyW));
        return;
    }
    if (isa == PairIsa::Avx)
        averagePairsAvx2(src, dst, bodyW);
    else
        averagePairsSse(src, dst, bodyW);
}

void blendRows(uint8_t *dst, const uint8_t *other, int count, PairIsa isa)
{
    if (isa == PairIsa::Avx)
        averageBytesAvx2(dst, other, dst, count);
    else
        averageBytesSse(dst, other, dst, count);
}

void interleave(uint8_t *dst, const uint8_t *r, const uint8_t *g, const uint8_t *b, int count)
{
    for (int x = 0; x < count; ++x)
    {
        uint8_t *pixel = dst + static_cast<size_t>(x) * 3u;
        pixel[0] = r[x];
        pixel[1] = g[x];
        pixel[2] = b[x];
    }
}

void boxRows(const RgbView &src, uint8_t *dst, int dstW, bool halfX, bool halfY, int y0, int y1,
             PairIsa isa)
{
    const int bodyW = (halfX && (src.width & 1) != 0) ? dstW - 1 : dstW;
    if (bodyW <= 0 || y1 <= y0)
        return;
    BoxScratch &scratch = boxScratch();
    fitBytes(scratch.rawR, src.width);
    fitBytes(scratch.rawG, src.width);
    fitBytes(scratch.rawB, src.width);
    fitBytes(scratch.outR, bodyW);
    fitBytes(scratch.outG, bodyW);
    fitBytes(scratch.outB, bodyW);
    for (int y = y0; y < y1; ++y)
    {
        const int sy = halfY ? y * 2 : y;
        deinterleave(packedRow(src, sy), src.width, scratch.rawR.data(), scratch.rawG.data(),
                     scratch.rawB.data());
        reducePlanar(scratch.rawR.data(), scratch.outR.data(), bodyW, halfX, isa);
        reducePlanar(scratch.rawG.data(), scratch.outG.data(), bodyW, halfX, isa);
        reducePlanar(scratch.rawB.data(), scratch.outB.data(), bodyW, halfX, isa);
        if (halfY)
        {
            deinterleave(packedRow(src, sy + 1), src.width, scratch.rawR.data(),
                         scratch.rawG.data(), scratch.rawB.data());
            reducePlanar(scratch.rawR.data(), scratch.rawR.data(), bodyW, halfX, isa);
            reducePlanar(scratch.rawG.data(), scratch.rawG.data(), bodyW, halfX, isa);
            reducePlanar(scratch.rawB.data(), scratch.rawB.data(), bodyW, halfX, isa);
            blendRows(scratch.outR.data(), scratch.rawR.data(), bodyW, isa);
            blendRows(scratch.outG.data(), scratch.rawG.data(), bodyW, isa);
            blendRows(scratch.outB.data(), scratch.rawB.data(), bodyW, isa);
        }
        uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        interleave(out, scratch.outR.data(), scratch.outG.data(), scratch.outB.data(), bodyW);
    }
}

int kernelSpan(const AxisKernel &kernel)
{
    int span = 0;
    for (int count : kernel.count)
    {
        if (count > span)
            span = count;
    }
    if (span > kMaxTaps)
        span = kMaxTaps;
    return span;
}

void horizontalTail(const uint8_t *r, const uint8_t *g, const uint8_t *b, int srcW,
                    const AxisKernel &kx, int x0, uint16_t *dstR, uint16_t *dstG, uint16_t *dstB)
{
    for (int x = x0; x < kx.dstCount; ++x)
    {
        const int taps = kx.count[static_cast<size_t>(x)];
        const int origin = kx.origin[static_cast<size_t>(x)];
        const int16_t *weight = kx.weight.data() + static_cast<size_t>(x) * kMaxTaps;
        int accR = 0;
        int accG = 0;
        int accB = 0;
        for (int tap = 0; tap < taps; ++tap)
        {
            const int sample = clampIndex(origin + tap, srcW);
            const int wr = static_cast<int>(weight[tap]);
            accR += static_cast<int>(r[sample]) * wr;
            accG += static_cast<int>(g[sample]) * wr;
            accB += static_cast<int>(b[sample]) * wr;
        }
        dstR[x] = narrowHorizontal(accR);
        dstG[x] = narrowHorizontal(accG);
        dstB[x] = narrowHorizontal(accB);
    }
}

void preparePlanar(const RgbView &src, int y, uint8_t *r, uint8_t *g, uint8_t *b)
{
    deinterleave(packedRow(src, y), src.width, r, g, b);
    for (int i = 0; i < 8; ++i)
    {
        r[src.width + i] = 0;
        g[src.width + i] = 0;
        b[src.width + i] = 0;
    }
}

MV_TARGET_AVX2 MV_NOINLINE void storeHorizontalAvx(__m256i acc, uint16_t *dst)
{
    __m256i shifted =
        _mm256_srai_epi32(_mm256_add_epi32(acc, _mm256_set1_epi32(kHorizBias)), kHorizShift);
    shifted = _mm256_max_epi32(shifted, _mm256_setzero_si256());
    shifted = _mm256_min_epi32(shifted, _mm256_set1_epi32(65535));
    alignas(32) int tmp[8];
    _mm256_store_si256(reinterpret_cast<__m256i *>(tmp), shifted);
    for (int lane = 0; lane < 8; ++lane)
        dst[lane] = static_cast<uint16_t>(tmp[lane]);
}

MV_TARGET_AVX2 MV_NOINLINE void horizontalAvx2(const uint8_t *r, const uint8_t *g, const uint8_t *b,
                                               int srcW, const AxisKernel &kx, uint16_t *dstR,
                                               uint16_t *dstG, uint16_t *dstB)
{
    const int span = kernelSpan(kx);
    const __m256i mask = _mm256_set1_epi32(0xff);
    int x = 0;
    for (; x + 8 <= kx.dstCount; x += 8)
    {
        __m256i accR = _mm256_setzero_si256();
        __m256i accG = _mm256_setzero_si256();
        __m256i accB = _mm256_setzero_si256();
        for (int tap = 0; tap < span; ++tap)
        {
            alignas(32) int index[8];
            alignas(32) int weight[8];
            bool live = false;
            for (int lane = 0; lane < 8; ++lane)
            {
                const int xx = x + lane;
                const int16_t *tapWeight = kx.weight.data() + static_cast<size_t>(xx) * kMaxTaps;
                const int wr =
                    tap < kx.count[static_cast<size_t>(xx)] ? static_cast<int>(tapWeight[tap]) : 0;
                weight[lane] = wr;
                index[lane] = clampIndex(kx.origin[static_cast<size_t>(xx)] + tap, srcW);
                live = live || wr != 0;
            }
            if (!live)
                continue;
            const __m256i vIndex = _mm256_load_si256(reinterpret_cast<const __m256i *>(index));
            const __m256i vWeight = _mm256_load_si256(reinterpret_cast<const __m256i *>(weight));
            const __m256i red = _mm256_and_si256(
                _mm256_i32gather_epi32(reinterpret_cast<const int *>(r), vIndex, 1), mask);
            const __m256i green = _mm256_and_si256(
                _mm256_i32gather_epi32(reinterpret_cast<const int *>(g), vIndex, 1), mask);
            const __m256i blue = _mm256_and_si256(
                _mm256_i32gather_epi32(reinterpret_cast<const int *>(b), vIndex, 1), mask);
            accR = _mm256_add_epi32(accR, _mm256_mullo_epi32(red, vWeight));
            accG = _mm256_add_epi32(accG, _mm256_mullo_epi32(green, vWeight));
            accB = _mm256_add_epi32(accB, _mm256_mullo_epi32(blue, vWeight));
        }
        storeHorizontalAvx(accR, dstR + x);
        storeHorizontalAvx(accG, dstG + x);
        storeHorizontalAvx(accB, dstB + x);
    }
    horizontalTail(r, g, b, srcW, kx, x, dstR, dstG, dstB);
}

MV_TARGET_SSE41 MV_NOINLINE void storeHorizontalSse(__m128i acc, uint16_t *dst)
{
    __m128i shifted = _mm_srai_epi32(_mm_add_epi32(acc, _mm_set1_epi32(kHorizBias)), kHorizShift);
    shifted = _mm_max_epi32(shifted, _mm_setzero_si128());
    shifted = _mm_min_epi32(shifted, _mm_set1_epi32(65535));
    alignas(16) int tmp[4];
    _mm_store_si128(reinterpret_cast<__m128i *>(tmp), shifted);
    for (int lane = 0; lane < 4; ++lane)
        dst[lane] = static_cast<uint16_t>(tmp[lane]);
}

MV_TARGET_SSE41 MV_NOINLINE void horizontalSse(const uint8_t *r, const uint8_t *g, const uint8_t *b,
                                               int srcW, const AxisKernel &kx, uint16_t *dstR,
                                               uint16_t *dstG, uint16_t *dstB)
{
    const int span = kernelSpan(kx);
    int x = 0;
    for (; x + 4 <= kx.dstCount; x += 4)
    {
        __m128i accR = _mm_setzero_si128();
        __m128i accG = _mm_setzero_si128();
        __m128i accB = _mm_setzero_si128();
        for (int tap = 0; tap < span; ++tap)
        {
            int index[4];
            int weight[4];
            bool live = false;
            for (int lane = 0; lane < 4; ++lane)
            {
                const int xx = x + lane;
                const int16_t *tapWeight = kx.weight.data() + static_cast<size_t>(xx) * kMaxTaps;
                const int wr =
                    tap < kx.count[static_cast<size_t>(xx)] ? static_cast<int>(tapWeight[tap]) : 0;
                weight[lane] = wr;
                index[lane] = clampIndex(kx.origin[static_cast<size_t>(xx)] + tap, srcW);
                live = live || wr != 0;
            }
            if (!live)
                continue;
            const __m128i vWeight = _mm_setr_epi32(weight[0], weight[1], weight[2], weight[3]);
            const __m128i red = _mm_setr_epi32(r[index[0]], r[index[1]], r[index[2]], r[index[3]]);
            const __m128i green =
                _mm_setr_epi32(g[index[0]], g[index[1]], g[index[2]], g[index[3]]);
            const __m128i blue = _mm_setr_epi32(b[index[0]], b[index[1]], b[index[2]], b[index[3]]);
            accR = _mm_add_epi32(accR, _mm_mullo_epi32(red, vWeight));
            accG = _mm_add_epi32(accG, _mm_mullo_epi32(green, vWeight));
            accB = _mm_add_epi32(accB, _mm_mullo_epi32(blue, vWeight));
        }
        storeHorizontalSse(accR, dstR + x);
        storeHorizontalSse(accG, dstG + x);
        storeHorizontalSse(accB, dstB + x);
    }
    horizontalTail(r, g, b, srcW, kx, x, dstR, dstG, dstB);
}

void verticalTail(const uint16_t *planeR, const uint16_t *planeG, const uint16_t *planeB,
                  int srcRow0, int srcH, const AxisKernel &ky, int y, int x0, int dstW,
                  uint8_t *dst)
{
    const int taps = ky.count[static_cast<size_t>(y)];
    const int origin = ky.origin[static_cast<size_t>(y)];
    const int16_t *weight = ky.weight.data() + static_cast<size_t>(y) * kMaxTaps;
    uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
    for (int x = x0; x < dstW; ++x)
    {
        int accR = 0;
        int accG = 0;
        int accB = 0;
        for (int tap = 0; tap < taps; ++tap)
        {
            const int sample = clampIndex(origin + tap, srcH);
            const size_t offset =
                static_cast<size_t>(sample - srcRow0) * static_cast<size_t>(dstW) +
                static_cast<size_t>(x);
            const int wr = static_cast<int>(weight[tap]);
            accR += static_cast<int>(planeR[offset]) * wr;
            accG += static_cast<int>(planeG[offset]) * wr;
            accB += static_cast<int>(planeB[offset]) * wr;
        }
        out[static_cast<size_t>(x) * 3u] = narrowVertical(accR);
        out[static_cast<size_t>(x) * 3u + 1u] = narrowVertical(accG);
        out[static_cast<size_t>(x) * 3u + 2u] = narrowVertical(accB);
    }
}

MV_TARGET_AVX2 MV_NOINLINE void narrowVerticalAvx(__m256i acc, int *tmp)
{
    __m256i shifted =
        _mm256_srai_epi32(_mm256_add_epi32(acc, _mm256_set1_epi32(kVertBias)), kVertShift);
    shifted = _mm256_max_epi32(shifted, _mm256_setzero_si256());
    shifted = _mm256_min_epi32(shifted, _mm256_set1_epi32(255));
    _mm256_store_si256(reinterpret_cast<__m256i *>(tmp), shifted);
}

MV_TARGET_AVX2 MV_NOINLINE void storeVerticalAvx(__m256i accR, __m256i accG, __m256i accB,
                                                 uint8_t *dst)
{
    alignas(32) int red[8];
    alignas(32) int green[8];
    alignas(32) int blue[8];
    narrowVerticalAvx(accR, red);
    narrowVerticalAvx(accG, green);
    narrowVerticalAvx(accB, blue);
    for (int lane = 0; lane < 8; ++lane)
    {
        uint8_t *pixel = dst + static_cast<size_t>(lane) * 3u;
        pixel[0] = static_cast<uint8_t>(red[lane]);
        pixel[1] = static_cast<uint8_t>(green[lane]);
        pixel[2] = static_cast<uint8_t>(blue[lane]);
    }
}

MV_TARGET_AVX2 MV_NOINLINE void verticalAvx2(const uint16_t *planeR, const uint16_t *planeG,
                                             const uint16_t *planeB, int srcRow0, int srcH,
                                             const AxisKernel &ky, int y0, int y1, int dstW,
                                             uint8_t *dst)
{
    for (int y = y0; y < y1; ++y)
    {
        const int taps = ky.count[static_cast<size_t>(y)];
        const int origin = ky.origin[static_cast<size_t>(y)];
        const int16_t *weight = ky.weight.data() + static_cast<size_t>(y) * kMaxTaps;
        uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        int x = 0;
        for (; x + 8 <= dstW; x += 8)
        {
            __m256i accR = _mm256_setzero_si256();
            __m256i accG = _mm256_setzero_si256();
            __m256i accB = _mm256_setzero_si256();
            for (int tap = 0; tap < taps; ++tap)
            {
                const int sample = clampIndex(origin + tap, srcH);
                const size_t offset =
                    static_cast<size_t>(sample - srcRow0) * static_cast<size_t>(dstW) +
                    static_cast<size_t>(x);
                const __m256i wr = _mm256_set1_epi32(static_cast<int>(weight[tap]));
                const __m256i red = _mm256_cvtepu16_epi32(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(planeR + offset)));
                const __m256i green = _mm256_cvtepu16_epi32(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(planeG + offset)));
                const __m256i blue = _mm256_cvtepu16_epi32(
                    _mm_loadu_si128(reinterpret_cast<const __m128i *>(planeB + offset)));
                accR = _mm256_add_epi32(accR, _mm256_mullo_epi32(red, wr));
                accG = _mm256_add_epi32(accG, _mm256_mullo_epi32(green, wr));
                accB = _mm256_add_epi32(accB, _mm256_mullo_epi32(blue, wr));
            }
            storeVerticalAvx(accR, accG, accB, out + static_cast<size_t>(x) * 3u);
        }
        verticalTail(planeR, planeG, planeB, srcRow0, srcH, ky, y, x, dstW, dst);
    }
}

MV_TARGET_SSE41 MV_NOINLINE void narrowVerticalSse(__m128i acc, int *tmp)
{
    __m128i shifted = _mm_srai_epi32(_mm_add_epi32(acc, _mm_set1_epi32(kVertBias)), kVertShift);
    shifted = _mm_max_epi32(shifted, _mm_setzero_si128());
    shifted = _mm_min_epi32(shifted, _mm_set1_epi32(255));
    _mm_store_si128(reinterpret_cast<__m128i *>(tmp), shifted);
}

MV_TARGET_SSE41 MV_NOINLINE void storeVerticalSse(__m128i accR, __m128i accG, __m128i accB,
                                                  uint8_t *dst)
{
    alignas(16) int red[4];
    alignas(16) int green[4];
    alignas(16) int blue[4];
    narrowVerticalSse(accR, red);
    narrowVerticalSse(accG, green);
    narrowVerticalSse(accB, blue);
    for (int lane = 0; lane < 4; ++lane)
    {
        uint8_t *pixel = dst + static_cast<size_t>(lane) * 3u;
        pixel[0] = static_cast<uint8_t>(red[lane]);
        pixel[1] = static_cast<uint8_t>(green[lane]);
        pixel[2] = static_cast<uint8_t>(blue[lane]);
    }
}

MV_TARGET_SSE41 MV_NOINLINE void verticalSse(const uint16_t *planeR, const uint16_t *planeG,
                                             const uint16_t *planeB, int srcRow0, int srcH,
                                             const AxisKernel &ky, int y0, int y1, int dstW,
                                             uint8_t *dst)
{
    for (int y = y0; y < y1; ++y)
    {
        const int taps = ky.count[static_cast<size_t>(y)];
        const int origin = ky.origin[static_cast<size_t>(y)];
        const int16_t *weight = ky.weight.data() + static_cast<size_t>(y) * kMaxTaps;
        uint8_t *out = dst + static_cast<size_t>(y) * static_cast<size_t>(dstW) * 3u;
        int x = 0;
        for (; x + 4 <= dstW; x += 4)
        {
            __m128i accR = _mm_setzero_si128();
            __m128i accG = _mm_setzero_si128();
            __m128i accB = _mm_setzero_si128();
            for (int tap = 0; tap < taps; ++tap)
            {
                const int sample = clampIndex(origin + tap, srcH);
                const size_t offset =
                    static_cast<size_t>(sample - srcRow0) * static_cast<size_t>(dstW) +
                    static_cast<size_t>(x);
                const __m128i wr = _mm_set1_epi32(static_cast<int>(weight[tap]));
                const __m128i red = _mm_cvtepu16_epi32(
                    _mm_loadl_epi64(reinterpret_cast<const __m128i *>(planeR + offset)));
                const __m128i green = _mm_cvtepu16_epi32(
                    _mm_loadl_epi64(reinterpret_cast<const __m128i *>(planeG + offset)));
                const __m128i blue = _mm_cvtepu16_epi32(
                    _mm_loadl_epi64(reinterpret_cast<const __m128i *>(planeB + offset)));
                accR = _mm_add_epi32(accR, _mm_mullo_epi32(red, wr));
                accG = _mm_add_epi32(accG, _mm_mullo_epi32(green, wr));
                accB = _mm_add_epi32(accB, _mm_mullo_epi32(blue, wr));
            }
            storeVerticalSse(accR, accG, accB, out + static_cast<size_t>(x) * 3u);
        }
        verticalTail(planeR, planeG, planeB, srcRow0, srcH, ky, y, x, dstW, dst);
    }
}

} // namespace

bool boxWriteBodyFast(const RgbView &src, uint8_t *dst, int dstW, int dstH, bool halfX, bool halfY,
                      int y0, int y1)
{
    (void)dstH;
    if (!viewIsPackedRgb(src))
        return false;
    const Isa isa = activeIsa();
    if (isa == Isa::Scalar)
        return false;
    if (y1 <= y0)
        return true;
    if (isa == Isa::Avx2)
        boxRows(src, dst, dstW, halfX, halfY, y0, y1, PairIsa::Avx);
    else
        boxRows(src, dst, dstW, halfX, halfY, y0, y1, PairIsa::Sse);
    return true;
}

bool convolveHorizontalFast(const RgbView &src, int y, const AxisKernel &kx, uint16_t *dstR,
                            uint16_t *dstG, uint16_t *dstB)
{
    if (!viewIsPackedRgb(src))
        return false;
    const Isa isa = activeIsa();
    if (isa == Isa::Scalar)
        return false;
    if (kx.dstCount <= 0)
        return true;
    HorizScratch &scratch = horizScratch();
    const int plane = src.width + 8;
    fitBytes(scratch.r, plane);
    fitBytes(scratch.g, plane);
    fitBytes(scratch.b, plane);
    preparePlanar(src, y, scratch.r.data(), scratch.g.data(), scratch.b.data());
    if (isa == Isa::Avx2)
        horizontalAvx2(scratch.r.data(), scratch.g.data(), scratch.b.data(), src.width, kx, dstR,
                       dstG, dstB);
    else
        horizontalSse(scratch.r.data(), scratch.g.data(), scratch.b.data(), src.width, kx, dstR,
                      dstG, dstB);
    return true;
}

bool convolveVerticalFast(const uint16_t *planeR, const uint16_t *planeG, const uint16_t *planeB,
                          int srcRow0, int srcH, const AxisKernel &ky, int y0, int y1, int dstW,
                          uint8_t *dst)
{
    const Isa isa = activeIsa();
    if (isa == Isa::Scalar || planeR == nullptr || dst == nullptr)
        return false;
    if (y1 <= y0 || dstW <= 0)
        return true;
    if (isa == Isa::Avx2)
        verticalAvx2(planeR, planeG, planeB, srcRow0, srcH, ky, y0, y1, dstW, dst);
    else
        verticalSse(planeR, planeG, planeB, srcRow0, srcH, ky, y0, y1, dstW, dst);
    return true;
}

} // namespace mviewer::core::resample_detail
