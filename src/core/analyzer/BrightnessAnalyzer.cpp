#include "core/analyzer/BrightnessAnalyzer.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <algorithm>
#include <cstdio>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define MV_BRIGHT_SSE2 1
#endif

bool BrightnessAnalyzer::compute(const ImageBuffer &v, int x0, int y0, int x1, int y1)
{
    if (x1 <= x0 || y1 <= y0)
        return false;
    const int cpp = v.channelsPerPixel();
    const int64_t n = static_cast<int64_t>(x1 - x0) * (y1 - y0);

    const bool isBGR = (v.format == PixelFormat::BGR24 || v.format == PixelFormat::BGRA32);
    const bool isGray = (v.format == PixelFormat::Grayscale8);

    int64_t iSum = 0;
    int iMn = 255, iMx = 0;

    if (isGray)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *line = v.data + static_cast<size_t>(y) * v.stride();
            int x = x0;
#if defined(MV_BRIGHT_SSE2)
            __m128i vzero = _mm_setzero_si128();
            __m128i vsum = _mm_setzero_si128();
            __m128i vmin = _mm_set1_epi8(static_cast<char>(iMn));
            __m128i vmax = _mm_set1_epi8(static_cast<char>(iMx));
            for (; x + 16 <= x1; x += 16)
            {
                __m128i val = _mm_loadu_si128(reinterpret_cast<const __m128i *>(line + x));
                vsum = _mm_add_epi64(vsum, _mm_sad_epu8(val, vzero));
                vmin = _mm_min_epu8(vmin, val);
                vmax = _mm_max_epu8(vmax, val);
            }
            alignas(16) int64_t sBuf[2];
            alignas(16) uint8_t mnBuf[16];
            alignas(16) uint8_t mxBuf[16];
            _mm_storeu_si128(reinterpret_cast<__m128i *>(sBuf), vsum);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(mnBuf), vmin);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(mxBuf), vmax);
            iSum += sBuf[0] + sBuf[1];
            for (int k = 0; k < 16; ++k)
            {
                iMn = std::min(iMn, static_cast<int>(mnBuf[k]));
                iMx = std::max(iMx, static_cast<int>(mxBuf[k]));
            }
#endif
            for (; x < x1; ++x)
            {
                const uint8_t val = line[x];
                iSum += val;
                iMn = std::min(iMn, static_cast<int>(val));
                iMx = std::max(iMx, static_cast<int>(val));
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
                iMn = std::min(iMn, l);
                iMx = std::max(iMx, l);
            }
        }
    }
    // Image sums fit comfortably in double mantissa for ROI sizes we support.
    m_result.avgLum =
        static_cast<double>(iSum) /
        static_cast<double>(
            n); // NOLINT(bugprone-narrowing-conversions,cppcoreguidelines-narrowing-conversions)
    m_result.minLum = static_cast<double>(iMn);
    m_result.maxLum = static_cast<double>(iMx);
    m_result.ok = true;
    return true;
}

bool BrightnessAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    return compute(frame.pixels().view(), 0, 0, frame.pixels().view().width,
                   frame.pixels().view().height);
}

bool BrightnessAnalyzer::analyzeRegion(const ImageFrame &frame,
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

std::string BrightnessAnalyzer::resultText() const
{
    char buf[128];
    std::snprintf(buf, sizeof(buf), "Lum: avg=%.1f  min=%.0f  max=%.0f", m_result.avgLum,
                  m_result.minLum, m_result.maxLum);
    return buf;
}

std::unordered_map<std::string, double> BrightnessAnalyzer::resultMetrics() const
{
    return {{"avgLuminance", m_result.avgLum},
            {"minLuminance", m_result.minLum},
            {"maxLuminance", m_result.maxLum}};
}
