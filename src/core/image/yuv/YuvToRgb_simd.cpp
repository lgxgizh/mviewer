#include "core/image/yuv/YuvToRgb.h"

#include "core/simd/CpuFeatures.h"

#include <cstddef>
#include <cstdint>

namespace mviewer::core::yuv
{
namespace
{

void interleaveRgb(const uint8_t *r, const uint8_t *g, const uint8_t *b, int n, uint8_t *dst)
{
    for (int i = 0; i < n; ++i)
    {
        uint8_t *px = dst + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(3);
        px[0] = r[i];
        px[1] = g[i];
        px[2] = b[i];
    }
}

#if defined(MV_ARCH_X86)

struct Sse2Pack
{
    __m128i yOff;
    __m128i cOff;
    __m128i two;
    __m128i cyPair;
    __m128i crvPair;
    __m128i gPair;
    __m128i bPair;
};

struct Avx2Pack
{
    __m256i yOff;
    __m256i cOff;
    __m256i two;
    __m256i cyPair;
    __m256i crvPair;
    __m256i gPair;
    __m256i bPair;
};

MV_TARGET_SSE2
__m128i pair16(int16_t a, int16_t b)
{
    return _mm_unpacklo_epi16(_mm_set1_epi16(a), _mm_set1_epi16(b));
}

MV_TARGET_SSE2
Sse2Pack makeSse2Pack(const YuvCoeffs &c)
{
    Sse2Pack p;
    p.yOff = _mm_set1_epi16(static_cast<int16_t>(c.yOff));
    p.cOff = _mm_set1_epi16(static_cast<int16_t>(c.cOff));
    p.two = _mm_set1_epi16(2);
    p.cyPair = pair16(static_cast<int16_t>(c.cy), 16384);
    p.crvPair = pair16(static_cast<int16_t>(c.crv), 0);
    p.gPair = pair16(static_cast<int16_t>(-c.cgu), static_cast<int16_t>(-c.cgv));
    p.bPair = pair16(static_cast<int16_t>(c.cbu), 0);
    return p;
}

MV_TARGET_SSE2
void sse2Yy(__m128i yd, const Sse2Pack &p, __m128i &lo, __m128i &hi)
{
    lo = _mm_madd_epi16(_mm_unpacklo_epi16(yd, p.two), p.cyPair);
    hi = _mm_madd_epi16(_mm_unpackhi_epi16(yd, p.two), p.cyPair);
}

MV_TARGET_SSE2
void sse2Store8(__m128i lo, __m128i hi, uint8_t *bytes)
{
    const __m128i s = _mm_packs_epi32(_mm_srai_epi32(lo, 16), _mm_srai_epi32(hi, 16));
    const __m128i packed = _mm_packus_epi16(s, _mm_setzero_si128());
    _mm_storel_epi64(reinterpret_cast<__m128i *>(bytes), packed);
}

MV_TARGET_SSE2
void sse2Gray8(const int16_t *y12, const Sse2Pack &p, uint8_t *dst)
{
    const __m128i yd =
        _mm_sub_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(y12)), p.yOff);
    __m128i lo;
    __m128i hi;
    sse2Yy(yd, p, lo, hi);
    alignas(16) uint8_t r[8];
    sse2Store8(lo, hi, r);
    interleaveRgb(r, r, r, 8, dst);
}

MV_TARGET_SSE2
void sse2Color8(const int16_t *y12, const int16_t *u12, const int16_t *v12, const Sse2Pack &p,
                uint8_t *dst)
{
    const __m128i yd =
        _mm_sub_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(y12)), p.yOff);
    const __m128i u =
        _mm_sub_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(u12)), p.cOff);
    const __m128i v =
        _mm_sub_epi16(_mm_loadu_si128(reinterpret_cast<const __m128i *>(v12)), p.cOff);
    __m128i yyLo;
    __m128i yyHi;
    sse2Yy(yd, p, yyLo, yyHi);
    const __m128i vuLo = _mm_unpacklo_epi16(v, u);
    const __m128i vuHi = _mm_unpackhi_epi16(v, u);
    const __m128i uvLo = _mm_unpacklo_epi16(u, v);
    const __m128i uvHi = _mm_unpackhi_epi16(u, v);
    alignas(16) uint8_t r[8];
    alignas(16) uint8_t g[8];
    alignas(16) uint8_t b[8];
    sse2Store8(_mm_add_epi32(yyLo, _mm_madd_epi16(vuLo, p.crvPair)),
               _mm_add_epi32(yyHi, _mm_madd_epi16(vuHi, p.crvPair)), r);
    sse2Store8(_mm_add_epi32(yyLo, _mm_madd_epi16(uvLo, p.gPair)),
               _mm_add_epi32(yyHi, _mm_madd_epi16(uvHi, p.gPair)), g);
    sse2Store8(_mm_add_epi32(yyLo, _mm_madd_epi16(uvLo, p.bPair)),
               _mm_add_epi32(yyHi, _mm_madd_epi16(uvHi, p.bPair)), b);
    interleaveRgb(r, g, b, 8, dst);
}

MV_TARGET_AVX2
__m256i pair16x(int16_t a, int16_t b)
{
    return _mm256_unpacklo_epi16(_mm256_set1_epi16(a), _mm256_set1_epi16(b));
}

MV_TARGET_AVX2
Avx2Pack makeAvx2Pack(const YuvCoeffs &c)
{
    Avx2Pack p;
    p.yOff = _mm256_set1_epi16(static_cast<int16_t>(c.yOff));
    p.cOff = _mm256_set1_epi16(static_cast<int16_t>(c.cOff));
    p.two = _mm256_set1_epi16(2);
    p.cyPair = pair16x(static_cast<int16_t>(c.cy), 16384);
    p.crvPair = pair16x(static_cast<int16_t>(c.crv), 0);
    p.gPair = pair16x(static_cast<int16_t>(-c.cgu), static_cast<int16_t>(-c.cgv));
    p.bPair = pair16x(static_cast<int16_t>(c.cbu), 0);
    return p;
}

MV_TARGET_AVX2
void avx2Yy(__m256i yd, const Avx2Pack &p, __m256i &lo, __m256i &hi)
{
    lo = _mm256_madd_epi16(_mm256_unpacklo_epi16(yd, p.two), p.cyPair);
    hi = _mm256_madd_epi16(_mm256_unpackhi_epi16(yd, p.two), p.cyPair);
}

MV_TARGET_AVX2
void avx2Store16(__m256i lo, __m256i hi, uint8_t *bytes)
{
    const __m256i s = _mm256_packs_epi32(_mm256_srai_epi32(lo, 16), _mm256_srai_epi32(hi, 16));
    __m256i packed = _mm256_packus_epi16(s, _mm256_setzero_si256());
    packed = _mm256_permute4x64_epi64(packed, _MM_SHUFFLE(3, 1, 2, 0));
    _mm_storeu_si128(reinterpret_cast<__m128i *>(bytes), _mm256_castsi256_si128(packed));
}

MV_TARGET_AVX2
void avx2Gray16(const int16_t *y12, const Avx2Pack &p, uint8_t *dst)
{
    const __m256i yd =
        _mm256_sub_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(y12)), p.yOff);
    __m256i lo;
    __m256i hi;
    avx2Yy(yd, p, lo, hi);
    alignas(16) uint8_t r[16];
    avx2Store16(lo, hi, r);
    interleaveRgb(r, r, r, 16, dst);
}

MV_TARGET_AVX2
void avx2Color16(const int16_t *y12, const int16_t *u12, const int16_t *v12, const Avx2Pack &p,
                 uint8_t *dst)
{
    const __m256i yd =
        _mm256_sub_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(y12)), p.yOff);
    const __m256i u =
        _mm256_sub_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(u12)), p.cOff);
    const __m256i v =
        _mm256_sub_epi16(_mm256_loadu_si256(reinterpret_cast<const __m256i *>(v12)), p.cOff);
    __m256i yyLo;
    __m256i yyHi;
    avx2Yy(yd, p, yyLo, yyHi);
    const __m256i vuLo = _mm256_unpacklo_epi16(v, u);
    const __m256i vuHi = _mm256_unpackhi_epi16(v, u);
    const __m256i uvLo = _mm256_unpacklo_epi16(u, v);
    const __m256i uvHi = _mm256_unpackhi_epi16(u, v);
    alignas(16) uint8_t r[16];
    alignas(16) uint8_t g[16];
    alignas(16) uint8_t b[16];
    avx2Store16(_mm256_add_epi32(yyLo, _mm256_madd_epi16(vuLo, p.crvPair)),
                _mm256_add_epi32(yyHi, _mm256_madd_epi16(vuHi, p.crvPair)), r);
    avx2Store16(_mm256_add_epi32(yyLo, _mm256_madd_epi16(uvLo, p.gPair)),
                _mm256_add_epi32(yyHi, _mm256_madd_epi16(uvHi, p.gPair)), g);
    avx2Store16(_mm256_add_epi32(yyLo, _mm256_madd_epi16(uvLo, p.bPair)),
                _mm256_add_epi32(yyHi, _mm256_madd_epi16(uvHi, p.bPair)), b);
    interleaveRgb(r, g, b, 16, dst);
}

#endif // MV_ARCH_X86

void scalarTail(const int16_t *y12, const int16_t *u12, const int16_t *v12, int done, int n,
                const YuvCoeffs &c, uint8_t *rgb24)
{
    if (done >= n)
        return;
    const bool gray = u12 == nullptr || v12 == nullptr;
    convertRowScalar(y12 + done, gray ? nullptr : u12 + done, gray ? nullptr : v12 + done, n - done,
                     c, rgb24 + static_cast<ptrdiff_t>(done) * static_cast<ptrdiff_t>(3));
}

} // namespace

#if defined(MV_ARCH_X86)

MV_TARGET_SSE2
void convertRowSse2(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                    const YuvCoeffs &c, uint8_t *rgb24)
{
    const bool gray = u12 == nullptr || v12 == nullptr;
    const Sse2Pack pack = makeSse2Pack(c);
    int i = 0;
    for (; i + 8 <= n; i += 8)
    {
        uint8_t *dst = rgb24 + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(3);
        if (gray)
            sse2Gray8(y12 + i, pack, dst);
        else
            sse2Color8(y12 + i, u12 + i, v12 + i, pack, dst);
    }
    scalarTail(y12, u12, v12, i, n, c, rgb24);
}

MV_TARGET_AVX2
void convertRowAvx2(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                    const YuvCoeffs &c, uint8_t *rgb24)
{
    const bool gray = u12 == nullptr || v12 == nullptr;
    const Avx2Pack pack = makeAvx2Pack(c);
    int i = 0;
    for (; i + 16 <= n; i += 16)
    {
        uint8_t *dst = rgb24 + static_cast<ptrdiff_t>(i) * static_cast<ptrdiff_t>(3);
        if (gray)
            avx2Gray16(y12 + i, pack, dst);
        else
            avx2Color16(y12 + i, u12 + i, v12 + i, pack, dst);
    }
    scalarTail(y12, u12, v12, i, n, c, rgb24);
}

#else

void convertRowSse2(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                    const YuvCoeffs &c, uint8_t *rgb24)
{
    convertRowScalar(y12, u12, v12, n, c, rgb24);
}

void convertRowAvx2(const int16_t *y12, const int16_t *u12, const int16_t *v12, int n,
                    const YuvCoeffs &c, uint8_t *rgb24)
{
    convertRowScalar(y12, u12, v12, n, c, rgb24);
}

#endif

RowConverterFn rowConverterFor(RowConverterKind kind)
{
    switch (kind)
    {
    case RowConverterKind::Avx2:
        return mviewer::core::CpuFeatures::hasAvx2() ? &convertRowAvx2 : &convertRowScalar;
    case RowConverterKind::Sse2:
        return mviewer::core::CpuFeatures::hasSSE2() ? &convertRowSse2 : &convertRowScalar;
    case RowConverterKind::Scalar:
        break;
    }
    return &convertRowScalar;
}

RowConverterKind bestRowConverterKind()
{
    if (mviewer::core::CpuFeatures::hasAvx2())
        return RowConverterKind::Avx2;
    if (mviewer::core::CpuFeatures::hasSSE2())
        return RowConverterKind::Sse2;
    return RowConverterKind::Scalar;
}

} // namespace mviewer::core::yuv
