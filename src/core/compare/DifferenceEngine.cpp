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

        for (; x + 4 <= w; x += 4)
        {
            alignas(16) uint8_t bufA[16]{};
            alignas(16) uint8_t bufB[16]{};
            std::memcpy(bufA, la + x * 3, 12);
            std::memcpy(bufB, lb + x * 3, 12);
            const __m128i va = _mm_load_si128(reinterpret_cast<const __m128i *>(bufA));
            const __m128i vb = _mm_load_si128(reinterpret_cast<const __m128i *>(bufB));

            const __m128i diff = _mm_or_si128(_mm_subs_epu8(va, vb), _mm_subs_epu8(vb, va));
            const __m128i R = _mm_shuffle_epi8(diff, maskR);
            const __m128i G = _mm_shuffle_epi8(diff, maskG);
            const __m128i B = _mm_shuffle_epi8(diff, maskB);

            const __m128i R16 = _mm_cvtepu8_epi16(R);
            const __m128i G16 = _mm_cvtepu8_epi16(G);
            const __m128i B16 = _mm_cvtepu8_epi16(B);

            const __m128i sum = _mm_add_epi16(_mm_add_epi16(R16, G16), B16);
            const __m128i avg16 = _mm_mulhi_epu16(sum, vdiv);
            const __m128i avg8 = _mm_packus_epi16(avg16, vzero);

            const __m128i under = _mm_subs_epu8(vthresh, avg8);
            const __m128i mask = _mm_cmpeq_epi8(under, vzero);
            const __m128i result = _mm_and_si128(avg8, mask);

            const uint32_t out4 = static_cast<uint32_t>(_mm_cvtsi128_si32(result));
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
            const __m256i sub1 = _mm256_subs_epu8(va, vb);
            const __m256i sub2 = _mm256_subs_epu8(vb, va);
            const __m256i diff = _mm256_or_si256(sub1, sub2);
            const __m256i under = _mm256_subs_epu8(vthresh, diff);
            const __m256i mask = _mm256_cmpeq_epi8(under, vzero);
            const __m256i result = _mm256_and_si256(diff, mask);
            _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), result);
        }
    }
    const __m128i vthresh128 = _mm_set1_epi8(static_cast<char>(threshold));
    const __m128i vzero128 = _mm_setzero_si128();
    for (; x + 16 <= w; x += 16)
    {
        const __m128i va = _mm_loadu_si128(reinterpret_cast<const __m128i *>(la + x));
        const __m128i vb = _mm_loadu_si128(reinterpret_cast<const __m128i *>(lb + x));
        const __m128i sub1 = _mm_subs_epu8(va, vb);
        const __m128i sub2 = _mm_subs_epu8(vb, va);
        const __m128i diff = _mm_or_si128(sub1, sub2);
        const __m128i under = _mm_subs_epu8(vthresh128, diff);
        const __m128i mask = _mm_cmpeq_epi8(under, vzero128);
        const __m128i result = _mm_and_si128(diff, mask);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), result);
    }
    for (; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x]) - static_cast<int>(lb[x]));
        dst[x] = (dr >= threshold) ? static_cast<uint8_t>(dr) : 0;
    }
}

static inline void diffRGBA32Row(const uint8_t *la, const uint8_t *lb, uint8_t *dst, int w,
                                 uint8_t threshold, bool useAvx2)
{
    int x = 0;
    if (useAvx2)
    {
        for (; x + 8 <= w; x += 8)
        {
            const __m256i va = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(la + x * 4));
            const __m256i vb = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(lb + x * 4));
            const __m256i sub1 = _mm256_subs_epu8(va, vb);
            const __m256i sub2 = _mm256_subs_epu8(vb, va);
            const __m256i diff = _mm256_or_si256(sub1, sub2);

            const __m128i dlo = _mm256_castsi256_si128(diff);
            const __m128i dhi = _mm256_extracti128_si256(diff, 1);

            auto calc4 = [&](__m128i d) -> uint32_t
            {
                const __m128i p01 = _mm_cvtepu8_epi16(d);
                const __m128i p23 = _mm_cvtepu8_epi16(_mm_srli_si128(d, 8));
                const int sum0 = _mm_extract_epi16(p01, 0) + _mm_extract_epi16(p01, 1) +
                                 _mm_extract_epi16(p01, 2);
                const int sum1 = _mm_extract_epi16(p01, 4) + _mm_extract_epi16(p01, 5) +
                                 _mm_extract_epi16(p01, 6);
                const int sum2 = _mm_extract_epi16(p23, 0) + _mm_extract_epi16(p23, 1) +
                                 _mm_extract_epi16(p23, 2);
                const int sum3 = _mm_extract_epi16(p23, 4) + _mm_extract_epi16(p23, 5) +
                                 _mm_extract_epi16(p23, 6);
                uint8_t d0 = static_cast<uint8_t>((sum0 * 21846) >> 16);
                uint8_t d1 = static_cast<uint8_t>((sum1 * 21846) >> 16);
                uint8_t d2 = static_cast<uint8_t>((sum2 * 21846) >> 16);
                uint8_t d3 = static_cast<uint8_t>((sum3 * 21846) >> 16);
                d0 = (d0 >= threshold) ? d0 : 0;
                d1 = (d1 >= threshold) ? d1 : 0;
                d2 = (d2 >= threshold) ? d2 : 0;
                d3 = (d3 >= threshold) ? d3 : 0;
                return static_cast<uint32_t>(d0) | (static_cast<uint32_t>(d1) << 8) |
                       (static_cast<uint32_t>(d2) << 16) | (static_cast<uint32_t>(d3) << 24);
            };

            const uint32_t r0 = calc4(dlo);
            const uint32_t r1 = calc4(dhi);
            std::memcpy(dst + x, &r0, 4);
            std::memcpy(dst + x + 4, &r1, 4);
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
                                 int cppA, int cppB, int roA0, int roA1, int roA2,
                                 int roB0, int roB1, int roB2, uint8_t threshold)
{
    for (int x = 0; x < w; ++x)
    {
        const int dr = std::abs(static_cast<int>(la[x * cppA + roA0]) -
                                static_cast<int>(lb[x * cppB + roB0]));
        const int dg = std::abs(static_cast<int>(la[x * cppA + roA1]) -
                                static_cast<int>(lb[x * cppB + roB1]));
        const int db = std::abs(static_cast<int>(la[x * cppA + roA2]) -
                                static_cast<int>(lb[x * cppB + roB2]));
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
            diffRGBA32Row(la, lb, dst, w, threshold, useAvx2);
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

    const bool useAvx2 = (cpp == 1 && ro == 0) && mviewer::core::CpuFeatures::hasAvx2();
    if (useAvx2)
    {
        const __m256i vthresh = _mm256_set1_epi8(static_cast<char>(threshold));
        const __m256i vzero = _mm256_setzero_si256();
        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            int x = 0;
            for (; x + 32 <= gray.width; x += 32)
            {
                const __m256i v = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(src + x));
                const __m256i under = _mm256_subs_epu8(vthresh, v);
                const __m256i mask = _mm256_cmpeq_epi8(under, vzero);
                const __m256i result = _mm256_and_si256(v, mask);
                _mm256_storeu_si256(reinterpret_cast<__m256i *>(dst + x), result);
            }
            for (; x < gray.width; ++x)
            {
                const uint8_t v = src[x];
                dst[x] = (v >= threshold) ? v : 0;
            }
        }
        return out;
    }
    else if (cpp == 1 && ro == 0)
    {
        const __m128i vthresh = _mm_set1_epi8(static_cast<char>(threshold));
        const __m128i vzero = _mm_setzero_si128();
        for (int y = 0; y < gray.height; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            int x = 0;
            for (; x + 16 <= gray.width; x += 16)
            {
                const __m128i v = _mm_loadu_si128(reinterpret_cast<const __m128i *>(src + x));
                const __m128i under = _mm_subs_epu8(vthresh, v);
                const __m128i mask = _mm_cmpeq_epi8(under, vzero);
                const __m128i result = _mm_and_si128(v, mask);
                _mm_storeu_si128(reinterpret_cast<__m128i *>(dst + x), result);
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
    {
        ImageData copy = makeImageData(gray.width, gray.height, gray.format);
        if (copy.isNull())
            return ImageData();
        if (gray.stride() == copy.stride())
        {
            std::memcpy(copy.buffer->data(), gray.buffer->data(),
                        static_cast<size_t>(gray.stride()) * gray.height);
        }
        else
        {
            for (int y = 0; y < gray.height; ++y)
            {
                const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
                uint8_t *dst = copy.buffer->data() + static_cast<size_t>(y) * copy.stride();
                std::memcpy(dst, src, static_cast<size_t>(gray.width) * gray.channelsPerPixel());
            }
        }
        return copy;
    }

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
        if (gray.stride() == static_cast<size_t>(gray.width) &&
            out.stride() == static_cast<size_t>(out.width))
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

DifferenceEngine::DiffStats DifferenceEngine::computeStats(const ImageData &grayDiff,
                                                           uint8_t threshold, int roiX, int roiY,
                                                           int roiW, int roiH)
{
    DiffStats s;
    if (grayDiff.isNull() || roiW <= 0 || roiH <= 0)
        return s;

    // Clip the ROI to the image bounds.
    int x0 = std::max(0, roiX);
    int y0 = std::max(0, roiY);
    int x1 = std::min(grayDiff.width, roiX + roiW);
    int y1 = std::min(grayDiff.height, roiY + roiH);
    if (x0 >= x1 || y0 >= y1)
        return s;

    const int cpp = grayDiff.channelsPerPixel();
    const int ro = channelOffset(grayDiff.format, 0);
    const int minDiff = std::max<int>(threshold, 1);

    long long sum = 0;
    long long diffCount = 0;
    int maxV = 0;
    const bool contiguous = (cpp == 1 && ro == 0 && x0 == 0 && y0 == 0 &&
                             x1 == grayDiff.width && y1 == grayDiff.height &&
                             grayDiff.stride() == static_cast<size_t>(grayDiff.width));
    if (contiguous)
    {
        const size_t total = static_cast<size_t>(grayDiff.width) * grayDiff.height;
        const uint8_t *src = grayDiff.buffer->data();
        for (size_t i = 0; i < total; ++i)
        {
            const int v = src[i];
            sum += v;
            diffCount += (v >= minDiff);
            maxV = std::max(maxV, v);
        }
    }
    else if (cpp == 1 && ro == 0)
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            for (int x = x0; x < x1; ++x)
            {
                const int v = src[x];
                sum += v;
                diffCount += (v >= minDiff);
                maxV = std::max(maxV, v);
            }
        }
    }
    else
    {
        for (int y = y0; y < y1; ++y)
        {
            const uint8_t *src =
                grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            for (int x = x0; x < x1; ++x)
            {
                const int v = src[x * cpp + ro];
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
            if (v < 128)
            {
                lut[v].r = 0;
                lut[v].g = static_cast<uint8_t>(v * 2);
                lut[v].b = static_cast<uint8_t>(255 - v * 2);
            }
            else
            {
                lut[v].r = static_cast<uint8_t>((v - 128) * 2);
                lut[v].g = static_cast<uint8_t>(255 - (v - 128) * 2);
                lut[v].b = 0;
            }
        }
        return lut;
    }();
    return table;
}

const auto &highlightIntensityLUT()
{
    static const auto table = []()
    {
        std::array<uint8_t, 256> lut{};
        for (int v = 0; v < 256; ++v)
            lut[v] = static_cast<uint8_t>(std::min(255, 80 + v * 2));
        return lut;
    }();
    return table;
}
} // namespace

ImageData DifferenceEngine::heatMap(const ImageData &gray)
{
    if (gray.isNull())
        return ImageData();
    const int w = gray.width;
    const int h = gray.height;
    const int cpp = gray.channelsPerPixel();
    const int ro = channelOffset(gray.format, 0);
    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();

    const auto &lut = heatLUT();
    if (cpp == 1 && ro == 0)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data() + static_cast<size_t>(y) * out.stride());
            for (int x = 0; x < w; ++x)
                dst[x] = lut[src[x]];
        }
    }
    else
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src = gray.buffer->data() + static_cast<size_t>(y) * gray.stride();
            auto *dst = reinterpret_cast<HeatRGB *>(out.buffer->data() + static_cast<size_t>(y) * out.stride());
            for (int x = 0; x < w; ++x)
                dst[x] = lut[src[x * cpp + ro]];
        }
    }
    return out;
}

ImageData DifferenceEngine::highlightMap(const ImageData &grayDiff, const ImageData &base,
                                         uint8_t threshold)
{
    if (grayDiff.isNull())
        return ImageData();
    const int w = grayDiff.width;
    const int h = grayDiff.height;
    const int cppD = grayDiff.channelsPerPixel();
    const int roD = channelOffset(grayDiff.format, 0);

    const bool hasBase =
        !base.isNull() && base.width >= w && base.height >= h &&
        (base.format == PixelFormat::RGB24 || base.format == PixelFormat::BGR24 ||
         base.format == PixelFormat::RGBA32 || base.format == PixelFormat::BGRA32 ||
         base.format == PixelFormat::Grayscale8);
    const int cppB = hasBase ? base.channelsPerPixel() : 0;
    const int roB0 = hasBase ? channelOffset(base.format, 0) : 0;
    const int roB1 = hasBase ? channelOffset(base.format, 1) : 0;
    const int roB2 = hasBase ? channelOffset(base.format, 2) : 0;
    const int minDiff = std::max<int>(threshold, 1);

    ImageData out = makeImageData(w, h, PixelFormat::RGB24);
    if (out.isNull())
        return ImageData();

    const auto &intLUT = highlightIntensityLUT();
    const bool isDirectDiff = (cppD == 1 && roD == 0);

    if (!hasBase)
    {
        for (int y = 0; y < h; ++y)
        {
            const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t v = isDirectDiff ? src[x] : src[x * cppD + roD];
                if (v >= minDiff)
                {
                    dst[x * 3 + 0] = intLUT[v];
                    dst[x * 3 + 1] = 0;
                    dst[x * 3 + 2] = 0;
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
            const uint8_t *src = grayDiff.buffer->data() + static_cast<size_t>(y) * grayDiff.stride();
            const uint8_t *bs = base.buffer->data() + static_cast<size_t>(y) * base.stride();
            uint8_t *dst = out.buffer->data() + static_cast<size_t>(y) * out.stride();
            for (int x = 0; x < w; ++x)
            {
                const uint8_t v = isDirectDiff ? src[x] : src[x * cppD + roD];
                if (v >= minDiff)
                {
                    dst[x * 3 + 0] = intLUT[v];
                    dst[x * 3 + 1] = 0;
                    dst[x * 3 + 2] = 0;
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
        for (int x = 0; x < w; ++x)
        {
            const uint8_t v = isDirectDiff ? src[x] : src[x * cppD + roD];
            if (v >= minDiff)
            {
                dst[x * 3 + 0] = intLUT[v];
                dst[x * 3 + 1] = 0;
                dst[x * 3 + 2] = 0;
            }
            else
            {
                const uint8_t *bp = bs + static_cast<size_t>(x) * cppB;
                const int r = isBGR ? bp[2] : bp[0];
                const int g = bp[1];
                const int b = isBGR ? bp[0] : bp[2];
                const uint8_t gray = static_cast<uint8_t>((r * 30 + g * 59 + b * 11) / 100);
                dst[x * 3 + 0] = gray;
                dst[x * 3 + 1] = gray;
                dst[x * 3 + 2] = gray;
            }
        }
    }
    return out;
}
