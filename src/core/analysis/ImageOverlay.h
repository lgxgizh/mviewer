#pragma once

#include "core/image/ImageBuffer.h"

#include "core/simd/CpuFeatures.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <cstring>

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
#include <immintrin.h>
#endif

#if defined(__GNUC__) || defined(__clang__)
#define MVIEWER_TARGET_AVX2 __attribute__((target("avx2")))
#define MVIEWER_TARGET_SSSE3 __attribute__((target("ssse3")))
#else
#define MVIEWER_TARGET_AVX2
#define MVIEWER_TARGET_SSSE3
#endif

namespace mviewer
{

// F4 (M22): live analysis overlays for the zoomable ImageViewer.
// Kept in core (pure std) so both the viewer and the standalone
// AnalysisOverlayDialog can share identical pixel math.
enum class OverlayMode : std::uint8_t
{
    None = 0,
    Zebra = 1,      // over/under-exposure clip indicators
    FalseColor = 2, // jet-mapped luminance
    ChannelR = 3,   // grayscale of the red plane
    ChannelG = 4,   // grayscale of the green plane
    ChannelB = 5,   // grayscale of the blue plane
    ChannelY = 6,   // grayscale of BT.601 luminance
    ChannelV = 7    // grayscale of HSV-V (max(R,G,B))
};

inline bool isChannelOverlay(OverlayMode mode)
{
    return mode == OverlayMode::ChannelR || mode == OverlayMode::ChannelG ||
           mode == OverlayMode::ChannelB || mode == OverlayMode::ChannelY ||
           mode == OverlayMode::ChannelV;
}

inline const char *overlayModeLabel(OverlayMode mode)
{
    switch (mode)
    {
    case OverlayMode::Zebra:
        return "Zebra";
    case OverlayMode::FalseColor:
        return "FalseColor";
    case OverlayMode::ChannelR:
        return "R";
    case OverlayMode::ChannelG:
        return "G";
    case OverlayMode::ChannelB:
        return "B";
    case OverlayMode::ChannelY:
        return "Y";
    case OverlayMode::ChannelV:
        return "V";
    case OverlayMode::None:
    default:
        return "RGB";
    }
}

inline uint8_t channelPlaneValue(OverlayMode mode, int r, int g, int b)
{
    switch (mode)
    {
    case OverlayMode::ChannelR:
        return static_cast<uint8_t>(r);
    case OverlayMode::ChannelG:
        return static_cast<uint8_t>(g);
    case OverlayMode::ChannelB:
        return static_cast<uint8_t>(b);
    case OverlayMode::ChannelV:
        return static_cast<uint8_t>(std::max({r, g, b}));
    case OverlayMode::ChannelY:
    default:
        return static_cast<uint8_t>(std::clamp(
            luminance(static_cast<uint8_t>(r), static_cast<uint8_t>(g), static_cast<uint8_t>(b)), 0,
            255));
    }
}

struct FalseColorLUT
{
    uint8_t r[256];
    uint8_t g[256];
    uint8_t b[256];

    constexpr FalseColorLUT() : r{}, g{}, b{}
    {
        for (int l = 0; l < 256; ++l)
        {
            const float t = static_cast<float>(l) / 255.0f;
            auto fclamp = [](float val) { return val < 0.0f ? 0.0f : (val > 1.0f ? 1.0f : val); };
            auto fabs_ = [](float val) { return val < 0.0f ? -val : val; };
            const float fr = fclamp(1.5f - fabs_(4.0f * t - 3.0f));
            const float fg = fclamp(1.5f - fabs_(4.0f * t - 2.0f));
            const float fb = fclamp(1.5f - fabs_(4.0f * t - 1.0f));
            r[l] = static_cast<uint8_t>(255.0f * fr);
            g[l] = static_cast<uint8_t>(255.0f * fg);
            b[l] = static_cast<uint8_t>(255.0f * fb);
        }
    }
};

inline constexpr FalseColorLUT s_falseColorLut{};

template <int Cpp, bool IsBgr> inline void applyFalseColorImpl(const ImageBuffer &v)
{
    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * Cpp);
    const size_t totalPixels = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);
    if (isContiguous)
    {
        uint8_t *p = v.data;
        for (size_t i = 0; i < totalPixels; ++i, p += Cpp)
        {
            uint8_t r, g, b;
            if constexpr (IsBgr)
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
            const int l = luminance(r, g, b);
            const uint8_t idx = static_cast<uint8_t>(std::clamp(l, 0, 255));
            if constexpr (IsBgr)
            {
                p[0] = s_falseColorLut.b[idx];
                p[1] = s_falseColorLut.g[idx];
                p[2] = s_falseColorLut.r[idx];
            }
            else
            {
                p[0] = s_falseColorLut.r[idx];
                p[1] = s_falseColorLut.g[idx];
                p[2] = s_falseColorLut.b[idx];
            }
        }
        return;
    }

    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            uint8_t *p = row + static_cast<size_t>(x) * Cpp;
            uint8_t r, g, b;
            if constexpr (IsBgr)
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
            const int l = luminance(r, g, b);
            const uint8_t idx = static_cast<uint8_t>(std::clamp(l, 0, 255));
            if constexpr (IsBgr)
            {
                p[0] = s_falseColorLut.b[idx];
                p[1] = s_falseColorLut.g[idx];
                p[2] = s_falseColorLut.r[idx];
            }
            else
            {
                p[0] = s_falseColorLut.r[idx];
                p[1] = s_falseColorLut.g[idx];
                p[2] = s_falseColorLut.b[idx];
            }
        }
    }
}

inline void applyFalseColor(ImageData &img)
{
    if (img.isNull() || img.width <= 0 || img.height <= 0)
        return;
    const ImageBuffer v = img.view();
    switch (v.format)
    {
    case PixelFormat::RGB24:
        applyFalseColorImpl<3, false>(v);
        break;
    case PixelFormat::RGBA32:
        applyFalseColorImpl<4, false>(v);
        break;
    case PixelFormat::BGR24:
        applyFalseColorImpl<3, true>(v);
        break;
    case PixelFormat::BGRA32:
        applyFalseColorImpl<4, true>(v);
        break;
    case PixelFormat::Grayscale8:
    {
        if (v.stride() == static_cast<ptrdiff_t>(v.width))
        {
            uint8_t *data = v.data;
            const size_t total = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);
            for (size_t i = 0; i < total; ++i)
                data[i] = s_falseColorLut.r[data[i]];
            break;
        }
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x)
                row[x] = s_falseColorLut.r[row[x]];
        }
        break;
    }
    default:
        break;
    }
}

#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)

MVIEWER_TARGET_SSSE3 inline __m128i makeChannelShuffleMask128(int chIdx)
{
    alignas(16) uint8_t m[16] = {static_cast<uint8_t>(chIdx),      static_cast<uint8_t>(chIdx),
                                 static_cast<uint8_t>(chIdx),      3,
                                 static_cast<uint8_t>(4 + chIdx),  static_cast<uint8_t>(4 + chIdx),
                                 static_cast<uint8_t>(4 + chIdx),  7,
                                 static_cast<uint8_t>(8 + chIdx),  static_cast<uint8_t>(8 + chIdx),
                                 static_cast<uint8_t>(8 + chIdx),  11,
                                 static_cast<uint8_t>(12 + chIdx), static_cast<uint8_t>(12 + chIdx),
                                 static_cast<uint8_t>(12 + chIdx), 15};
    return _mm_loadu_si128(reinterpret_cast<const __m128i *>(m));
}

MVIEWER_TARGET_AVX2 inline void applyChannelRgbAvx2(uint8_t *data, size_t count, int chIdx)
{
    const __m128i mask128 = makeChannelShuffleMask128(chIdx);
    const __m256i mask256 = _mm256_broadcastsi128_si256(mask128);
    for (size_t i = 0; i + 8 <= count; i += 8)
    {
        const __m256i in = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i * 4));
        const __m256i out = _mm256_shuffle_epi8(in, mask256);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(data + i * 4), out);
    }
}

MVIEWER_TARGET_SSSE3 inline void applyChannelRgbSsse3(uint8_t *data, size_t count, int chIdx)
{
    const __m128i mask128 = makeChannelShuffleMask128(chIdx);
    for (size_t i = 0; i + 4 <= count; i += 4)
    {
        const __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i * 4));
        const __m128i out = _mm_shuffle_epi8(in, mask128);
        _mm_storeu_si128(reinterpret_cast<__m128i *>(data + i * 4), out);
    }
}

MVIEWER_TARGET_AVX2 inline void applyChannelVAvx2(uint8_t *data, size_t count)
{
    alignas(16) static const uint8_t m0[16] = {0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12};
    alignas(16) static const uint8_t m1[16] = {1, 1, 1, 1, 5, 5, 5, 5, 9, 9, 9, 9, 13, 13, 13, 13};
    alignas(16) static const uint8_t m2[16] = {2,  2,  2,  2,  6,  6,  6,  6,
                                               10, 10, 10, 10, 14, 14, 14, 14};
    alignas(16) static const uint8_t alphaMask[16] = {0, 0, 0, 0xFF, 0, 0, 0, 0xFF,
                                                      0, 0, 0, 0xFF, 0, 0, 0, 0xFF};

    const __m256i mask0 =
        _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i *>(m0)));
    const __m256i mask1 =
        _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i *>(m1)));
    const __m256i mask2 =
        _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i *>(m2)));
    const __m256i aMask =
        _mm256_broadcastsi128_si256(_mm_loadu_si128(reinterpret_cast<const __m128i *>(alphaMask)));

    for (size_t i = 0; i + 8 <= count; i += 8)
    {
        const __m256i in = _mm256_loadu_si256(reinterpret_cast<const __m256i *>(data + i * 4));
        const __m256i c0 = _mm256_shuffle_epi8(in, mask0);
        const __m256i c1 = _mm256_shuffle_epi8(in, mask1);
        const __m256i c2 = _mm256_shuffle_epi8(in, mask2);
        const __m256i maxVal = _mm256_max_epu8(_mm256_max_epu8(c0, c1), c2);
        const __m256i out = _mm256_blendv_epi8(maxVal, in, aMask);
        _mm256_storeu_si256(reinterpret_cast<__m256i *>(data + i * 4), out);
    }
}

MVIEWER_TARGET_SSSE3 inline void applyChannelVSsse3(uint8_t *data, size_t count)
{
    alignas(16) static const uint8_t m0[16] = {0, 0, 0, 0, 4, 4, 4, 4, 8, 8, 8, 8, 12, 12, 12, 12};
    alignas(16) static const uint8_t m1[16] = {1, 1, 1, 1, 5, 5, 5, 5, 9, 9, 9, 9, 13, 13, 13, 13};
    alignas(16) static const uint8_t m2[16] = {2,  2,  2,  2,  6,  6,  6,  6,
                                               10, 10, 10, 10, 14, 14, 14, 14};
    alignas(16) static const uint8_t alphaMask[16] = {0, 0, 0, 0xFF, 0, 0, 0, 0xFF,
                                                      0, 0, 0, 0xFF, 0, 0, 0, 0xFF};

    const __m128i mask0 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(m0));
    const __m128i mask1 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(m1));
    const __m128i mask2 = _mm_loadu_si128(reinterpret_cast<const __m128i *>(m2));
    const __m128i aMask = _mm_loadu_si128(reinterpret_cast<const __m128i *>(alphaMask));

    for (size_t i = 0; i + 4 <= count; i += 4)
    {
        const __m128i in = _mm_loadu_si128(reinterpret_cast<const __m128i *>(data + i * 4));
        const __m128i c0 = _mm_shuffle_epi8(in, mask0);
        const __m128i c1 = _mm_shuffle_epi8(in, mask1);
        const __m128i c2 = _mm_shuffle_epi8(in, mask2);
        const __m128i maxVal = _mm_max_epu8(_mm_max_epu8(c0, c1), c2);
        const __m128i out = _mm_or_si128(_mm_andnot_si128(aMask, maxVal), _mm_and_si128(aMask, in));
        _mm_storeu_si128(reinterpret_cast<__m128i *>(data + i * 4), out);
    }
}
#endif

template <bool IsBgr> inline void applyChannel4(const ImageBuffer &v, OverlayMode mode)
{
    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * 4);
    const size_t totalPixels = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);

    int chIdx = -1;
    if (mode == OverlayMode::ChannelR)
        chIdx = IsBgr ? 2 : 0;
    else if (mode == OverlayMode::ChannelG)
        chIdx = 1;
    else if (mode == OverlayMode::ChannelB)
        chIdx = IsBgr ? 0 : 2;

    if (chIdx >= 0)
    {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        const bool hasAvx2 = mviewer::core::CpuFeatures::hasAvx2();
        const bool hasSsse3 = mviewer::core::CpuFeatures::hasSsse3();

        if (isContiguous)
        {
            uint8_t *data = v.data;
            size_t i = 0;
            if (hasAvx2)
            {
                applyChannelRgbAvx2(data, totalPixels, chIdx);
                i = totalPixels & ~size_t(7);
            }
            else if (hasSsse3)
            {
                applyChannelRgbSsse3(data, totalPixels, chIdx);
                i = totalPixels & ~size_t(3);
            }
            for (; i < totalPixels; ++i)
            {
                uint8_t *p = data + i * 4;
                const uint8_t val = p[chIdx];
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
            return;
        }

        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            const size_t width = static_cast<size_t>(v.width);
            size_t x = 0;
            if (hasAvx2)
            {
                applyChannelRgbAvx2(row, width, chIdx);
                x = width & ~size_t(7);
            }
            else if (hasSsse3)
            {
                applyChannelRgbSsse3(row, width, chIdx);
                x = width & ~size_t(3);
            }
            for (; x < width; ++x)
            {
                uint8_t *p = row + x * 4;
                const uint8_t val = p[chIdx];
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        return;
#endif
    }
    else if (mode == OverlayMode::ChannelV)
    {
#if defined(__x86_64__) || defined(_M_X64) || defined(__i386__) || defined(_M_IX86)
        const bool hasAvx2 = mviewer::core::CpuFeatures::hasAvx2();
        const bool hasSsse3 = mviewer::core::CpuFeatures::hasSsse3();

        if (isContiguous)
        {
            uint8_t *data = v.data;
            size_t i = 0;
            if (hasAvx2)
            {
                applyChannelVAvx2(data, totalPixels);
                i = totalPixels & ~size_t(7);
            }
            else if (hasSsse3)
            {
                applyChannelVSsse3(data, totalPixels);
                i = totalPixels & ~size_t(3);
            }
            for (; i < totalPixels; ++i)
            {
                uint8_t *p = data + i * 4;
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
            return;
        }

        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
            const size_t width = static_cast<size_t>(v.width);
            size_t x = 0;
            if (hasAvx2)
            {
                applyChannelVAvx2(row, width);
                x = width & ~size_t(7);
            }
            else if (hasSsse3)
            {
                applyChannelVSsse3(row, width);
                x = width & ~size_t(3);
            }
            for (; x < width; ++x)
            {
                uint8_t *p = row + x * 4;
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        return;
#endif
    }

    // Scalar fallback (including ChannelY and non-x86)
    if (isContiguous)
    {
        uint8_t *p = v.data;
        if (chIdx >= 0)
        {
            for (size_t i = 0; i < totalPixels; ++i, p += 4)
            {
                const uint8_t val = p[chIdx];
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        else if (mode == OverlayMode::ChannelV)
        {
            for (size_t i = 0; i < totalPixels; ++i, p += 4)
            {
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        else // ChannelY
        {
            for (size_t i = 0; i < totalPixels; ++i, p += 4)
            {
                const uint8_t b = IsBgr ? p[0] : p[2];
                const uint8_t g = p[1];
                const uint8_t r = IsBgr ? p[2] : p[0];
                const uint8_t val = static_cast<uint8_t>(std::clamp(luminance(r, g, b), 0, 255));
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        return;
    }

    if (chIdx >= 0)
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x, p += 4)
            {
                const uint8_t val = p[chIdx];
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
    }
    else if (mode == OverlayMode::ChannelV)
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x, p += 4)
            {
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
    }
    else // ChannelY
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x, p += 4)
            {
                const uint8_t b = IsBgr ? p[0] : p[2];
                const uint8_t g = p[1];
                const uint8_t r = IsBgr ? p[2] : p[0];
                const uint8_t val = static_cast<uint8_t>(std::clamp(luminance(r, g, b), 0, 255));
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
    }
}

template <bool IsBgr> inline void applyChannel3(const ImageBuffer &v, OverlayMode mode)
{
    const bool isContiguous = (v.stride() == static_cast<ptrdiff_t>(v.width) * 3);
    const size_t totalPixels = static_cast<size_t>(v.width) * static_cast<size_t>(v.height);

    int chIdx = -1;
    if (mode == OverlayMode::ChannelR)
        chIdx = IsBgr ? 2 : 0;
    else if (mode == OverlayMode::ChannelG)
        chIdx = 1;
    else if (mode == OverlayMode::ChannelB)
        chIdx = IsBgr ? 0 : 2;

    if (isContiguous)
    {
        uint8_t *p = v.data;
        if (chIdx >= 0)
        {
            for (size_t i = 0; i < totalPixels; ++i, p += 3)
            {
                const uint8_t val = p[chIdx];
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        else if (mode == OverlayMode::ChannelV)
        {
            for (size_t i = 0; i < totalPixels; ++i, p += 3)
            {
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        else // ChannelY
        {
            for (size_t i = 0; i < totalPixels; ++i, p += 3)
            {
                const uint8_t b = IsBgr ? p[0] : p[2];
                const uint8_t g = p[1];
                const uint8_t r = IsBgr ? p[2] : p[0];
                const uint8_t val = static_cast<uint8_t>(std::clamp(luminance(r, g, b), 0, 255));
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
        return;
    }

    if (chIdx >= 0)
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x, p += 3)
            {
                const uint8_t val = p[chIdx];
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
    }
    else if (mode == OverlayMode::ChannelV)
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x, p += 3)
            {
                const uint8_t val = std::max({p[0], p[1], p[2]});
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
    }
    else // ChannelY
    {
        for (int y = 0; y < v.height; ++y)
        {
            uint8_t *p = v.data + static_cast<size_t>(y) * v.stride();
            for (int x = 0; x < v.width; ++x, p += 3)
            {
                const uint8_t b = IsBgr ? p[0] : p[2];
                const uint8_t g = p[1];
                const uint8_t r = IsBgr ? p[2] : p[0];
                const uint8_t val = static_cast<uint8_t>(std::clamp(luminance(r, g, b), 0, 255));
                p[0] = val;
                p[1] = val;
                p[2] = val;
            }
        }
    }
}

inline void applyChannelOverlay(ImageData &img, OverlayMode mode)
{
    if (img.isNull() || img.width <= 0 || img.height <= 0 || img.format == PixelFormat::Grayscale8)
        return;
    const ImageBuffer v = img.view();
    switch (v.format)
    {
    case PixelFormat::RGB24:
        applyChannel3<false>(v, mode);
        break;
    case PixelFormat::RGBA32:
        applyChannel4<false>(v, mode);
        break;
    case PixelFormat::BGR24:
        applyChannel3<true>(v, mode);
        break;
    case PixelFormat::BGRA32:
        applyChannel4<true>(v, mode);
        break;
    default:
        break;
    }
}

template <int Cpp, bool IsBgr> inline void applyZebraImpl(const ImageBuffer &v, int lo, int hi)
{
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            if (((x + y) & 7) >= 4)
                continue;
            uint8_t *p = row + static_cast<size_t>(x) * Cpp;
            uint8_t r, g, b;
            if constexpr (IsBgr)
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
            const int l = luminance(r, g, b);
            if (l >= hi || l <= lo)
            {
                const uint8_t v0 = (l >= hi) ? 0 : 255;
                p[0] = v0;
                p[1] = v0;
                p[2] = v0;
            }
        }
    }
}

inline void applyZebraGrayscale(const ImageBuffer &v, int lo, int hi)
{
    for (int y = 0; y < v.height; ++y)
    {
        uint8_t *row = v.data + static_cast<size_t>(y) * v.stride();
        for (int x = 0; x < v.width; ++x)
        {
            if (((x + y) & 7) >= 4)
                continue;
            const int l = row[x];
            if (l >= hi || l <= lo)
                row[x] = (l >= hi) ? 0 : 255;
        }
    }
}

inline void applyZebraOverlay(ImageData &img, int zebraThresholdPct)
{
    if (img.isNull() || img.width <= 0 || img.height <= 0)
        return;
    const int thr = std::clamp(zebraThresholdPct, 1, 40);
    const int lo = (thr * 255) / 100;
    const int hi = 255 - lo;
    const ImageBuffer v = img.view();
    switch (v.format)
    {
    case PixelFormat::RGB24:
        applyZebraImpl<3, false>(v, lo, hi);
        break;
    case PixelFormat::RGBA32:
        applyZebraImpl<4, false>(v, lo, hi);
        break;
    case PixelFormat::BGR24:
        applyZebraImpl<3, true>(v, lo, hi);
        break;
    case PixelFormat::BGRA32:
        applyZebraImpl<4, true>(v, lo, hi);
        break;
    case PixelFormat::Grayscale8:
        applyZebraGrayscale(v, lo, hi);
        break;
    default:
        break;
    }
}

// Apply an overlay in place on an ImageData. Supports RGB24 / RGBA32 /
// BGR24 / BGRA32 / Grayscale8. Alpha is preserved for the *A variants.
// Safe to call every frame: it operates on the passed buffer only.
inline void applyOverlay(ImageData &img, OverlayMode mode, int zebraThresholdPct)
{
    if (mode == OverlayMode::None || img.isNull())
        return;

    if (mode == OverlayMode::FalseColor)
    {
        applyFalseColor(img);
        return;
    }

    if (isChannelOverlay(mode))
    {
        applyChannelOverlay(img, mode);
        return;
    }

    if (mode == OverlayMode::Zebra)
    {
        applyZebraOverlay(img, zebraThresholdPct);
    }
}

} // namespace mviewer
