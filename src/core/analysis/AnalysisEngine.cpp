#include "core/analysis/AnalysisEngine.h"

#include "core/compare/DifferenceEngine.h"
#include "core/image/QtConvert.h"
#include "domain/Selection.h"

#include <QImage>
#include <algorithm>
#include <cmath>
#include <cstring>

#if defined(__SSE2__) || defined(_M_X64) || (defined(_M_IX86_FP) && _M_IX86_FP >= 2)
#include <emmintrin.h>
#define MVIEWER_HAVE_SSE2 1
#endif

// 内部实现：把 ImageData 转成 QImage 做像素级统计，算法逻辑保持不变。
// header 不暴露 Qt；这里在 .cpp 内部使用 Qt 作为实现细节。

ImageStats AnalysisEngine::computeStats(const ImageData &imgData)
{
    // 全图统计：ROI 设为整图
    mviewer::domain::Selection full;
    full.x = 0;
    full.y = 0;
    full.width = imgData.width;
    full.height = imgData.height;
    return computeStatsROI(imgData, full);
}

namespace
{

#ifdef MVIEWER_HAVE_SSE2
static inline int64_t sse2_sum_sq_diff(const uint8_t *a, const uint8_t *b, size_t len)
{
    size_t i = 0;
    int64_t total = 0;
    __m128i vsum64 = _mm_setzero_si128();
    const __m128i zero = _mm_setzero_si128();
    __m128i vsum32 = _mm_setzero_si128();
    size_t block_count = 0;

    for (; i + 16 <= len; i += 16)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + i));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + i));

        const __m128i a_lo = _mm_unpacklo_epi8(va, zero);
        const __m128i b_lo = _mm_unpacklo_epi8(vb, zero);
        const __m128i diff_lo = _mm_sub_epi16(a_lo, b_lo);
        const __m128i sq_lo = _mm_madd_epi16(diff_lo, diff_lo);

        const __m128i a_hi = _mm_unpackhi_epi8(va, zero);
        const __m128i b_hi = _mm_unpackhi_epi8(vb, zero);
        const __m128i diff_hi = _mm_sub_epi16(a_hi, b_hi);
        const __m128i sq_hi = _mm_madd_epi16(diff_hi, diff_hi);

        vsum32 = _mm_add_epi32(vsum32, _mm_add_epi32(sq_lo, sq_hi));

        if (++block_count == 8192)
        {
            const __m128i sum64_lo = _mm_unpacklo_epi32(vsum32, zero);
            const __m128i sum64_hi = _mm_unpackhi_epi32(vsum32, zero);
            vsum64 = _mm_add_epi64(vsum64, _mm_add_epi64(sum64_lo, sum64_hi));
            vsum32 = zero;
            block_count = 0;
        }
    }
    if (block_count > 0)
    {
        const __m128i sum64_lo = _mm_unpacklo_epi32(vsum32, zero);
        const __m128i sum64_hi = _mm_unpackhi_epi32(vsum32, zero);
        vsum64 = _mm_add_epi64(vsum64, _mm_add_epi64(sum64_lo, sum64_hi));
    }
    alignas(16) int64_t res[2];
    _mm_storeu_si128(reinterpret_cast<__m128i *>(res), vsum64);
    total = res[0] + res[1];

    for (; i < len; ++i)
    {
        const int d = static_cast<int>(a[i]) - static_cast<int>(b[i]);
        total += static_cast<int64_t>(d) * d;
    }
    return total;
}

static inline int64_t sse2_sum_sq_diff_rgba(const uint8_t *a, const uint8_t *b, size_t numPixels)
{
    size_t i = 0;
    int64_t total = 0;
    __m128i vsum64 = _mm_setzero_si128();
    const __m128i zero = _mm_setzero_si128();
    const __m128i maskRGB16 = _mm_setr_epi16(-1, -1, -1, 0, -1, -1, -1, 0);
    __m128i vsum32 = _mm_setzero_si128();
    size_t block_count = 0;

    for (; i + 4 <= numPixels; i += 4)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(a + i * 4));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(b + i * 4));

        const __m128i a_lo = _mm_unpacklo_epi8(va, zero);
        const __m128i b_lo = _mm_unpacklo_epi8(vb, zero);
        const __m128i diff_lo = _mm_and_si128(_mm_sub_epi16(a_lo, b_lo), maskRGB16);
        const __m128i sq_lo = _mm_madd_epi16(diff_lo, diff_lo);

        const __m128i a_hi = _mm_unpackhi_epi8(va, zero);
        const __m128i b_hi = _mm_unpackhi_epi8(vb, zero);
        const __m128i diff_hi = _mm_and_si128(_mm_sub_epi16(a_hi, b_hi), maskRGB16);
        const __m128i sq_hi = _mm_madd_epi16(diff_hi, diff_hi);

        vsum32 = _mm_add_epi32(vsum32, _mm_add_epi32(sq_lo, sq_hi));

        if (++block_count == 8192)
        {
            const __m128i sum64_lo = _mm_unpacklo_epi32(vsum32, zero);
            const __m128i sum64_hi = _mm_unpackhi_epi32(vsum32, zero);
            vsum64 = _mm_add_epi64(vsum64, _mm_add_epi64(sum64_lo, sum64_hi));
            vsum32 = zero;
            block_count = 0;
        }
    }
    if (block_count > 0)
    {
        const __m128i sum64_lo = _mm_unpacklo_epi32(vsum32, zero);
        const __m128i sum64_hi = _mm_unpackhi_epi32(vsum32, zero);
        vsum64 = _mm_add_epi64(vsum64, _mm_add_epi64(sum64_lo, sum64_hi));
    }
    alignas(16) int64_t res[2];
    _mm_storeu_si128(reinterpret_cast<__m128i *>(res), vsum64);
    total = res[0] + res[1];

    for (; i < numPixels; ++i)
    {
        const size_t offset = i * 4;
        const int dr = static_cast<int>(a[offset + 0]) - static_cast<int>(b[offset + 0]);
        const int dg = static_cast<int>(a[offset + 1]) - static_cast<int>(b[offset + 1]);
        const int db = static_cast<int>(a[offset + 2]) - static_cast<int>(b[offset + 2]);
        total += dr * dr + dg * dg + db * db;
    }
    return total;
}
#endif

ImageStats computeStatsGrayscale(const ImageBuffer &vbuf, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    long long sum = 0;
    if (rx == 0 && rw == vbuf.width && vbuf.stride() == static_cast<size_t>(rw))
    {
        const uint8_t *p = vbuf.data + static_cast<size_t>(ry) * rw;
        const size_t total = static_cast<size_t>(rw) * rh;
        for (size_t i = 0; i < total; ++i)
        {
            const uint8_t val = p[i];
            sum += val;
            ++s.histLum[val];
        }
    }
    else
    {
        for (int y = ry; y < ry + rh; ++y)
        {
            const uint8_t *line = vbuf.data + static_cast<size_t>(y) * vbuf.stride();
            for (int x = rx; x < rx + rw; ++x)
            {
                const uint8_t val = line[x];
                sum += val;
                ++s.histLum[val];
            }
        }
    }
    for (int i = 0; i < 256; ++i)
    {
        s.histV[i] = s.histLum[i];
        s.histR[i] = s.histLum[i];
        s.histG[i] = s.histLum[i];
        s.histB[i] = s.histLum[i];
    }
    const int count = rw * rh;
    s.pixelCount = count;
    if (count > 0)
    {
        const double mean = static_cast<double>(sum) / count;
        s.lumMean = mean;
        s.vMean = mean;
        s.rMean = mean;
        s.gMean = mean;
        s.bMean = mean;
    }
    return s;
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
#ifdef MVIEWER_HAVE_SSE2
            const __m128i zero = _mm_setzero_si128();
            __m128i vsumAA = _mm_setzero_si128();
            __m128i vsumBB = _mm_setzero_si128();
            __m128i vsumAB = _mm_setzero_si128();

            for (int y = 0; y < block; ++y)
            {
                const uint8_t *la = linesA[y] + bx;
                const uint8_t *lb = linesB[y] + bx;

                const __m128i va = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(la));
                const __m128i vb = _mm_loadl_epi64(reinterpret_cast<const __m128i *>(lb));

                sumA += _mm_cvtsi128_si32(_mm_sad_epu8(va, zero));
                sumB += _mm_cvtsi128_si32(_mm_sad_epu8(vb, zero));

                const __m128i va16 = _mm_unpacklo_epi8(va, zero);
                const __m128i vb16 = _mm_unpacklo_epi8(vb, zero);

                vsumAA = _mm_add_epi32(vsumAA, _mm_madd_epi16(va16, va16));
                vsumBB = _mm_add_epi32(vsumBB, _mm_madd_epi16(vb16, vb16));
                vsumAB = _mm_add_epi32(vsumAB, _mm_madd_epi16(va16, vb16));
            }

            auto hsum32 = [](__m128i v) -> int
            {
                v = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(1, 0, 3, 2)));
                v = _mm_add_epi32(v, _mm_shuffle_epi32(v, _MM_SHUFFLE(2, 3, 0, 1)));
                return _mm_cvtsi128_si32(v);
            };

            sumAA = hsum32(vsumAA);
            sumBB = hsum32(vsumBB);
            sumAB = hsum32(vsumAB);
#else
            for (int y = 0; y < block; ++y)
            {
                const uint8_t *la = linesA[y] + bx;
                const uint8_t *lb = linesB[y] + bx;
                for (int x = 0; x < block; ++x)
                {
                    const int pa = la[x];
                    const int pb = lb[x];
                    sumA += pa;
                    sumB += pb;
                    sumAA += pa * pa;
                    sumBB += pb * pb;
                    sumAB += pa * pb;
                }
            }
#endif
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
        for (int x = 1; x < w - 1; ++x)
        {
            const int lap = static_cast<int>(prev[x]) + curr[x - 1] + curr[x + 1] + next[x] -
                            4 * static_cast<int>(curr[x]);
            sum += lap;
            sumSq += static_cast<int64_t>(lap) * lap;
        }
        prev = curr;
        curr = next;
    }
    const double mean = static_cast<double>(sum) / count;
    const double variance = static_cast<double>(sumSq) / count - mean * mean;
    return std::max(0.0, variance);
}

ImageStats computeStatsFallback(const ImageData &imgData, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    const QImage image = mvcore::toQImage(imgData).convertToFormat(QImage::Format_RGB32);
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
    int count = 0;
    for (int y = ry; y < ry + rh; ++y)
    {
        const QRgb *line = reinterpret_cast<const QRgb *>(image.constScanLine(y));
        for (int x = rx; x < rx + rw; ++x)
        {
            const QRgb c = line[x];
            const int r = qRed(c), g = qGreen(c), b = qBlue(c);
            sumR += r;
            sumG += g;
            sumB += b;
            const int lum = (19595 * r + 38470 * g + 7471 * b) >> 16;
            sumL += lum;
            const int v = std::max({r, g, b});
            sumV += v;
            ++s.histLum[lum];
            ++s.histV[std::clamp(v, 0, 255)];
            ++s.histR[std::clamp(r, 0, 255)];
            ++s.histG[std::clamp(g, 0, 255)];
            ++s.histB[std::clamp(b, 0, 255)];
            ++count;
        }
    }
    s.pixelCount = count;
    if (count > 0)
    {
        s.lumMean = static_cast<double>(sumL) / count;
        s.vMean = static_cast<double>(sumV) / count;
        s.rMean = static_cast<double>(sumR) / count;
        s.gMean = static_cast<double>(sumG) / count;
        s.bMean = static_cast<double>(sumB) / count;
    }
    return s;
}

template <PixelFormat Fmt>
ImageStats computeStatsColor(const ImageBuffer &vbuf, int rx, int ry, int rw, int rh)
{
    ImageStats s;
    constexpr int cpp = (Fmt == PixelFormat::RGB24 || Fmt == PixelFormat::BGR24) ? 3 : 4;
    constexpr bool isBGR = (Fmt == PixelFormat::BGR24 || Fmt == PixelFormat::BGRA32);
    long long sumL = 0, sumR = 0, sumG = 0, sumB = 0, sumV = 0;
    const size_t stride = vbuf.stride();

    for (int y = ry; y < ry + rh; ++y)
    {
        const uint8_t *line = vbuf.data + static_cast<size_t>(y) * stride;
        for (int x = rx; x < rx + rw; ++x)
        {
            const uint8_t *p = line + static_cast<size_t>(x) * cpp;
            int r, g, b;
            if constexpr (isBGR)
            {
                b = p[0];
                g = p[1];
                r = p[2];
            }
            else
            {
                r = p[0];
                g = p[1];
                b = p[2];
            }
            sumR += r;
            sumG += g;
            sumB += b;
            const int lum = (19595 * r + 38470 * g + 7471 * b) >> 16;
            sumL += lum;
            const int v = std::max({r, g, b});
            sumV += v;
            ++s.histLum[lum];
            ++s.histV[v];
            ++s.histR[r];
            ++s.histG[g];
            ++s.histB[b];
        }
    }
    const int count = rw * rh;
    s.pixelCount = count;
    if (count > 0)
    {
        s.lumMean = static_cast<double>(sumL) / count;
        s.vMean = static_cast<double>(sumV) / count;
        s.rMean = static_cast<double>(sumR) / count;
        s.gMean = static_cast<double>(sumG) / count;
        s.bMean = static_cast<double>(sumB) / count;
    }
    return s;
}

} // namespace

ImageStats AnalysisEngine::computeStatsROI(const ImageData &imgData,
                                           const mviewer::domain::Selection &region)
{
    if (imgData.isNull())
        return {};
    const int w = imgData.width;
    const int h = imgData.height;
    if (w <= 0 || h <= 0)
        return {};

    // 裁剪 ROI 到图像边界 (正负坐标均安全截断)
    const long long x0ll = std::clamp<long long>(region.x, 0, w);
    const long long y0ll = std::clamp<long long>(region.y, 0, h);
    const long long x1ll =
        std::clamp<long long>(static_cast<long long>(region.x) + region.width, 0, w);
    const long long y1ll =
        std::clamp<long long>(static_cast<long long>(region.y) + region.height, 0, h);
    const int rx = static_cast<int>(std::min(x0ll, x1ll));
    const int ry = static_cast<int>(std::min(y0ll, y1ll));
    const int x1 = static_cast<int>(std::max(x0ll, x1ll));
    const int y1 = static_cast<int>(std::max(y0ll, y1ll));
    const int rw = x1 - rx;
    const int rh = y1 - ry;
    if (rw <= 0 || rh <= 0)
        return {};

    const ImageBuffer vbuf = imgData.view();
    const PixelFormat fmt = imgData.format;
    if (fmt == PixelFormat::Grayscale8)
        return computeStatsGrayscale(vbuf, rx, ry, rw, rh);

    switch (fmt)
    {
    case PixelFormat::RGB24:
        return computeStatsColor<PixelFormat::RGB24>(vbuf, rx, ry, rw, rh);
    case PixelFormat::BGR24:
        return computeStatsColor<PixelFormat::BGR24>(vbuf, rx, ry, rw, rh);
    case PixelFormat::RGBA32:
        return computeStatsColor<PixelFormat::RGBA32>(vbuf, rx, ry, rw, rh);
    case PixelFormat::BGRA32:
        return computeStatsColor<PixelFormat::BGRA32>(vbuf, rx, ry, rw, rh);
    default:
        break;
    }

    return computeStatsFallback(imgData, rx, ry, rw, rh);
}

ImageData AnalysisEngine::differenceMap(const ImageData &aData, const ImageData &bData)
{
    // ONE implementation: DifferenceEngine handles every PixelFormat natively
    // (per-format channel offsets) and produces the same Grayscale8 map, so this
    // entry point only supplies the "no threshold" default. Keeping a second
    // copy here meant two sets of numerics for one user-visible feature.
    return DifferenceEngine::differenceMap(aData, bData, 0);
}

namespace
{

double psnrFromSumSq(int64_t sumSq, long long denom)
{
    const double mse = static_cast<double>(sumSq) / static_cast<double>(denom);
    if (mse <= 1e-10)
        return 100.0; // 完美一致(而非 inf)
    return 10.0 * std::log10(65025.0 / mse);
}

int64_t psnrSumSqGray(const ImageBuffer &va, const ImageBuffer &vb, int w, int h)
{
    int64_t sumSq = 0;
    if (w == va.width && va.stride() == static_cast<size_t>(w) &&
        vb.stride() == static_cast<size_t>(w))
    {
        const size_t total = static_cast<size_t>(w) * static_cast<size_t>(h);
#ifdef MVIEWER_HAVE_SSE2
        sumSq = sse2_sum_sq_diff(va.data, vb.data, total);
#else
        const uint8_t *la = va.data;
        const uint8_t *lb = vb.data;
        for (size_t i = 0; i < total; ++i)
        {
            const int d = static_cast<int>(la[i]) - static_cast<int>(lb[i]);
            sumSq += d * d;
        }
#endif
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
#ifdef MVIEWER_HAVE_SSE2
            sumSq += sse2_sum_sq_diff(la, lb, static_cast<size_t>(w));
#else
            for (int x = 0; x < w; ++x)
            {
                const int d = static_cast<int>(la[x]) - static_cast<int>(lb[x]);
                sumSq += d * d;
            }
#endif
        }
    }
    return sumSq;
}

int64_t psnrSumSq3(const ImageBuffer &va, const ImageBuffer &vb, int w, int h, bool contiguous)
{
    int64_t sumSq = 0;
    if (contiguous)
    {
        const size_t totalBytes = static_cast<size_t>(w) * 3u * static_cast<size_t>(h);
#ifdef MVIEWER_HAVE_SSE2
        sumSq = sse2_sum_sq_diff(va.data, vb.data, totalBytes);
#else
        const uint8_t *la = va.data;
        const uint8_t *lb = vb.data;
        for (size_t i = 0; i < totalBytes; ++i)
        {
            const int d = static_cast<int>(la[i]) - static_cast<int>(lb[i]);
            sumSq += d * d;
        }
#endif
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
#ifdef MVIEWER_HAVE_SSE2
            sumSq += sse2_sum_sq_diff(la, lb, static_cast<size_t>(w) * 3u);
#else
            for (int x = 0; x < w; ++x)
            {
                const size_t offset = static_cast<size_t>(x) * 3;
                const int dr = static_cast<int>(la[offset + 0]) - static_cast<int>(lb[offset + 0]);
                const int dg = static_cast<int>(la[offset + 1]) - static_cast<int>(lb[offset + 1]);
                const int db = static_cast<int>(la[offset + 2]) - static_cast<int>(lb[offset + 2]);
                sumSq += dr * dr + dg * dg + db * db;
            }
#endif
        }
    }
    return sumSq;
}

int64_t psnrSumSq4(const ImageBuffer &va, const ImageBuffer &vb, int w, int h, bool contiguous)
{
    int64_t sumSq = 0;
    if (contiguous)
    {
        const size_t totalPixels = static_cast<size_t>(w) * static_cast<size_t>(h);
#ifdef MVIEWER_HAVE_SSE2
        sumSq = sse2_sum_sq_diff_rgba(va.data, vb.data, totalPixels);
#else
        const uint8_t *la = va.data;
        const uint8_t *lb = vb.data;
        for (size_t i = 0; i < totalPixels; ++i)
        {
            const size_t offset = i * 4;
            const int dr = static_cast<int>(la[offset + 0]) - static_cast<int>(lb[offset + 0]);
            const int dg = static_cast<int>(la[offset + 1]) - static_cast<int>(lb[offset + 1]);
            const int db = static_cast<int>(la[offset + 2]) - static_cast<int>(lb[offset + 2]);
            sumSq += dr * dr + dg * dg + db * db;
        }
#endif
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *la = va.data + static_cast<size_t>(y) * va.stride();
            const uint8_t *lb = vb.data + static_cast<size_t>(y) * vb.stride();
#ifdef MVIEWER_HAVE_SSE2
            sumSq += sse2_sum_sq_diff_rgba(la, lb, static_cast<size_t>(w));
#else
            for (int x = 0; x < w; ++x)
            {
                const size_t offset = static_cast<size_t>(x) * 4;
                const int dr = static_cast<int>(la[offset + 0]) - static_cast<int>(lb[offset + 0]);
                const int dg = static_cast<int>(la[offset + 1]) - static_cast<int>(lb[offset + 1]);
                const int db = static_cast<int>(la[offset + 2]) - static_cast<int>(lb[offset + 2]);
                sumSq += dr * dr + dg * dg + db * db;
            }
#endif
        }
    }
    return sumSq;
}

int64_t psnrSumSqQImage(const ImageData &aData, const ImageData &bData, int w, int h)
{
    int64_t sumSq = 0;
    QImage aa = mvcore::toQImage(aData).convertToFormat(QImage::Format_RGB32);
    QImage bb = mvcore::toQImage(bData).convertToFormat(QImage::Format_RGB32);
    for (int y = 0; y < h; ++y)
    {
        const uint8_t *la = aa.constScanLine(y);
        const uint8_t *lb = bb.constScanLine(y);
#ifdef MVIEWER_HAVE_SSE2
        sumSq += sse2_sum_sq_diff_rgba(la, lb, static_cast<size_t>(w));
#else
        const QRgb *qla = reinterpret_cast<const QRgb *>(la);
        const QRgb *qlb = reinterpret_cast<const QRgb *>(lb);
        for (int x = 0; x < w; ++x)
        {
            const int dr = static_cast<int>(qRed(qla[x])) - qRed(qlb[x]);
            const int dg = static_cast<int>(qGreen(qla[x])) - qGreen(qlb[x]);
            const int db = static_cast<int>(qBlue(qla[x])) - qBlue(qlb[x]);
            sumSq += dr * dr + dg * dg + db * db;
        }
#endif
    }
    return sumSq;
}

} // namespace

double AnalysisEngine::psnr(const ImageData &aData, const ImageData &bData)
{
    const int w = std::min(aData.width, bData.width);
    const int h = std::min(aData.height, bData.height);
    if (w <= 0 || h <= 0)
        return 0.0;

    const long long n = 1LL * w * h;
    const bool isSameFormat = (aData.format == bData.format);

    if (isSameFormat &&
        (aData.format == PixelFormat::RGB24 || aData.format == PixelFormat::BGR24 ||
         aData.format == PixelFormat::RGBA32 || aData.format == PixelFormat::BGRA32 ||
         aData.format == PixelFormat::Grayscale8))
    {
        const ImageBuffer va = aData.view();
        const ImageBuffer vb = bData.view();
        const int cpp = va.channelsPerPixel();
        if (aData.format == PixelFormat::Grayscale8)
            return psnrFromSumSq(psnrSumSqGray(va, vb, w, h), n);

        const bool contiguous =
            (w == va.width && va.stride() == static_cast<size_t>(w) * static_cast<size_t>(cpp) &&
             vb.stride() == static_cast<size_t>(w) * static_cast<size_t>(cpp));
        if (cpp == 3)
            return psnrFromSumSq(psnrSumSq3(va, vb, w, h, contiguous), n * 3);
        return psnrFromSumSq(psnrSumSq4(va, vb, w, h, contiguous), n * 3);
    }

    return psnrFromSumSq(psnrSumSqQImage(aData, bData, w, h), n * 3);
}

double AnalysisEngine::ssim(const ImageData &aData, const ImageData &bData)
{
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

ImageData AnalysisEngine::heatMap(const ImageData &grayData)
{
    if (grayData.isNull())
        return ImageData();
    return DifferenceEngine::heatMap(grayData);
}
