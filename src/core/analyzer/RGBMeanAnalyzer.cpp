#include "core/analyzer/RGBMeanAnalyzer.h"

#include "core/image/ImageStats.h"

#include <algorithm>
#include <cmath>
#include <iomanip>
#include <sstream>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define MV_RGB_SSE2 1
#endif

namespace
{
bool computeRGB(const ImageData &img, const mviewer::domain::Selection &region,
                RGBMeanAnalyzer::Result &out)
{
    out = {};
    if (img.isNull() || region.isEmpty())
        return false;

    const long long x0ll = std::clamp<long long>(region.x, 0, img.width);
    const long long y0ll = std::clamp<long long>(region.y, 0, img.height);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, img.width);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, img.height);
    const int x0 = static_cast<int>(std::min(x0ll, x1ll));
    const int y0 = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    if (x1 <= x0 || y1 <= y0)
        return false;

    uint64_t sumR = 0, sumG = 0, sumB = 0;
    uint64_t sumR2 = 0, sumG2 = 0, sumB2 = 0;
    int64_t pixelCount = 0;
    const ImageBuffer view = img.view();
    const int cpp = view.channelsPerPixel();
    const bool isBGR = (view.format == PixelFormat::BGR24 || view.format == PixelFormat::BGRA32);
    const bool isGray = (view.format == PixelFormat::Grayscale8);

    for (int y = y0; y < y1; ++y)
    {
        const uint8_t *row = view.data + static_cast<size_t>(y) * view.stride();
        if (isGray)
        {
            int x = x0;
#if defined(MV_RGB_SSE2)
            __m128i vzero = _mm_setzero_si128();
            __m128i vsum = _mm_setzero_si128();
            __m128i vsum2 = _mm_setzero_si128();
            for (; x + 16 <= x1; x += 16)
            {
                __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(row + x));
                vsum = _mm_add_epi64(vsum, _mm_sad_epu8(v, vzero));
                __m128i vlo = _mm_unpacklo_epi8(v, vzero);
                __m128i vhi = _mm_unpackhi_epi8(v, vzero);
                __m128i sqlo = _mm_madd_epi16(vlo, vlo);
                __m128i sqhi = _mm_madd_epi16(vhi, vhi);
                __m128i sqlo_0 = _mm_unpacklo_epi32(sqlo, vzero);
                __m128i sqlo_1 = _mm_unpackhi_epi32(sqlo, vzero);
                __m128i sqhi_0 = _mm_unpacklo_epi32(sqhi, vzero);
                __m128i sqhi_1 = _mm_unpackhi_epi32(sqhi, vzero);
                vsum2 = _mm_add_epi64(vsum2, _mm_add_epi64(sqlo_0, sqlo_1));
                vsum2 = _mm_add_epi64(vsum2, _mm_add_epi64(sqhi_0, sqhi_1));
            }
            alignas(16) uint64_t sBuf[2];
            alignas(16) uint64_t s2Buf[2];
            _mm_storeu_si128(reinterpret_cast<__m128i *>(sBuf), vsum);
            _mm_storeu_si128(reinterpret_cast<__m128i *>(s2Buf), vsum2);
            sumR += sBuf[0] + sBuf[1];
            sumR2 += s2Buf[0] + s2Buf[1];
            pixelCount += (x - x0);
#endif
            for (; x < x1; ++x)
            {
                const uint8_t v = row[x];
                sumR += v;
                sumR2 += static_cast<uint64_t>(v) * v;
                ++pixelCount;
            }
        }
        else
        {
            const uint8_t *p = row + static_cast<size_t>(x0) * cpp;
            for (int x = x0; x < x1; ++x, p += cpp)
            {
                const uint8_t r = isBGR ? p[2] : p[0];
                const uint8_t g = p[1];
                const uint8_t b = isBGR ? p[0] : p[2];
                sumR += r;
                sumG += g;
                sumB += b;
                sumR2 += static_cast<uint64_t>(r) * r;
                sumG2 += static_cast<uint64_t>(g) * g;
                sumB2 += static_cast<uint64_t>(b) * b;
                ++pixelCount;
            }
        }
    }

    if (pixelCount <= 0)
        return false;

    if (isGray)
    {
        sumG = sumB = sumR;
        sumG2 = sumB2 = sumR2;
    }

    const double count = static_cast<double>(pixelCount);
    out.rMean = static_cast<double>(sumR) / count;
    out.gMean = static_cast<double>(sumG) / count;
    out.bMean = static_cast<double>(sumB) / count;
    out.rStd = std::sqrt(std::max(0.0, static_cast<double>(sumR2) / count - out.rMean * out.rMean));
    out.gStd = std::sqrt(std::max(0.0, static_cast<double>(sumG2) / count - out.gMean * out.gMean));
    out.bStd = std::sqrt(std::max(0.0, static_cast<double>(sumB2) / count - out.bMean * out.bMean));
    if (sumG != 0)
    {
        out.rOverG = static_cast<double>(sumR) / static_cast<double>(sumG);
        out.bOverG = static_cast<double>(sumB) / static_cast<double>(sumG);
        out.ratiosValid = true;
    }
    out.pixelCount = pixelCount;
    out.ok = true;
    return true;
}
} // namespace

bool RGBMeanAnalyzer::analyze(const ImageFrame &frame)
{
    if (frame.pixels().isNull())
        return false;
    const ImageData &pixels = frame.pixels();
    return computeRGB(pixels, mviewer::domain::Selection{0, 0, pixels.width, pixels.height},
                      m_result);
}

bool RGBMeanAnalyzer::analyzeRegion(const ImageFrame &frame,
                                    const mviewer::domain::Selection &region)
{
    return computeRGB(frame.pixels(), region, m_result);
}

std::string RGBMeanAnalyzer::resultText() const
{
    if (!m_result.ok)
        return "RGB mean: unavailable";
    std::ostringstream text;
    text << std::fixed << std::setprecision(2) << "RGB mean: (" << m_result.rMean << ", "
         << m_result.gMean << ", " << m_result.bMean << ")  std: (" << m_result.rStd << ", "
         << m_result.gStd << ", " << m_result.bStd << ")  ";
    if (m_result.ratiosValid)
    {
        text << std::setprecision(4) << "R/G " << m_result.rOverG << "  B/G " << m_result.bOverG;
    }
    else
        text << "R/G —  B/G —";
    return text.str();
}

std::unordered_map<std::string, double> RGBMeanAnalyzer::resultMetrics() const
{
    return {{"rMean", m_result.rMean},
            {"gMean", m_result.gMean},
            {"bMean", m_result.bMean},
            {"rStd", m_result.rStd},
            {"gStd", m_result.gStd},
            {"bStd", m_result.bStd},
            {"rOverG", m_result.rOverG},
            {"bOverG", m_result.bOverG},
            {"pixelCount", static_cast<double>(m_result.pixelCount)}};
}
