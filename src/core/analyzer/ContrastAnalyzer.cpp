#include "core/analyzer/ContrastAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cmath>
#include <cstdio>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define MV_CONTRAST_SSE2 1
#endif

bool ContrastAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0)
        return false;
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);

    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    int64_t iSum = 0;
    int64_t iSum2 = 0;

    if (isGray)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            int x = x0;
#if defined(MV_CONTRAST_SSE2)
            __m128i vzero = _mm_setzero_si128();
            __m128i vsum = _mm_setzero_si128();
            __m128i vsum2 = _mm_setzero_si128();
            for (; x + 16 <= x1; x += 16)
            {
                __m128i val = _mm_loadu_si128(reinterpret_cast<const __m128i *>(line + x));
                vsum = _mm_add_epi64(vsum, _mm_sad_epu8(val, vzero));
                __m128i vlo = _mm_unpacklo_epi8(val, vzero);
                __m128i vhi = _mm_unpackhi_epi8(val, vzero);
                __m128i sqlo = _mm_madd_epi16(vlo, vlo);
                __m128i sqhi = _mm_madd_epi16(vhi, vhi);
                __m128i sqlo_0 = _mm_unpacklo_epi32(sqlo, vzero);
                __m128i sqlo_1 = _mm_unpackhi_epi32(sqlo, vzero);
                __m128i sqhi_0 = _mm_unpacklo_epi32(sqhi, vzero);
                __m128i sqhi_1 = _mm_unpackhi_epi32(sqhi, vzero);
                vsum2 = _mm_add_epi64(vsum2, _mm_add_epi64(sqlo_0, sqlo_1));
                vsum2 = _mm_add_epi64(vsum2, _mm_add_epi64(sqhi_0, sqhi_1));
            }
            alignas(16) int64_t sBuf[2];
            alignas(16) int64_t s2Buf[2];
            _mm_storeu_si128(reinterpret_cast<__m128i *>(sBuf), vsum);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(s2Buf), vsum2);
            iSum += sBuf[0] + sBuf[1];
            iSum2 += s2Buf[0] + s2Buf[1];
#endif
            for (; x < x1; ++x)
            {
                const uint8_t val = line[x];
                iSum += val;
                iSum2 += static_cast<int64_t>(val) * val;
            }
        }
    }
    else
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            const uint8_t *p = line + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                const int r = isBGR ? p[2] : p[0];
                const int g = p[1];
                const int b = isBGR ? p[0] : p[2];
                const int l = (19595 * r + 38470 * g + 7471 * b) >> 16;
                iSum += l;
                iSum2 += static_cast<int64_t>(l) * l;
            }
        }
    }
    // Image sums fit comfortably in double mantissa for ROI sizes we support.
    m_result.mean = static_cast<double>(iSum) / static_cast<double>(n); // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
    m_result.rms = std::sqrt(std::max(
        0.0, static_cast<double>(iSum2) / static_cast<double>(n) - // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
                 m_result.mean * m_result.mean));
    m_result.ok = true;
    return true;
}

bool ContrastAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageBuffer v = frame.pixels().view();
    return compute(v, 0, 0, v.width, v.height);
}

bool ContrastAnalyzer::analyzeRegion(const ImageFrame &frame,
                                     const mviewer::domain::Selection &region)
{
    if (frame.pixels().isNull() || region.isEmpty())
        return false;
    const ImageBuffer v = frame.pixels().view();
    const long long x0ll = std::clamp<long long>(region.x, 0, v.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, v.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, v.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, v.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x1 <= x0 || y1 <= y0)
        return false;
    return compute(v, x0, y0, x1, y1);
}

std::string ContrastAnalyzer::resultText() const
{
    char buf[80];
    std::snprintf(buf, sizeof(buf), "RMS contrast: %.2f  mean lum: %.1f", m_result.rms,
                  m_result.mean);
    return buf;
}

std::unordered_map<std::string, double> ContrastAnalyzer::resultMetrics() const
{
    return {{"rmsContrast", m_result.rms}, {"meanLuminance", m_result.mean}};
}
