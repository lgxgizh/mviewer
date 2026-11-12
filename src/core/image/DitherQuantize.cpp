#include "core/image/DitherQuantize.h"

#include <QImage>

#include <cstdint>

namespace
{

// Deterministic 8×8 Bayer ordered-dither matrix, values 0..63.
// Recursive Bayer lattice from the seed {{0, 2}, {3, 1}}:
//   M(2n) = | 4M+0  4M+2 |
//           | 4M+3  4M+1 |
// Indexed [y & 7][x & 7]. Each value in 0..63 appears once.
constexpr std::uint8_t kBayer8[8][8] = {
    {0, 32, 8, 40, 2, 34, 10, 42},  {48, 16, 56, 24, 50, 18, 58, 26},
    {12, 44, 4, 36, 14, 46, 6, 38}, {60, 28, 52, 20, 62, 30, 54, 22},
    {3, 35, 11, 43, 1, 33, 9, 41},  {51, 19, 59, 27, 49, 17, 57, 25},
    {15, 47, 7, 39, 13, 45, 5, 37}, {63, 31, 55, 23, 61, 29, 53, 21},
};

constexpr bool bayerCovers0Through63()
{
    bool seen[64] = {};
    for (int y = 0; y < 8; ++y)
    {
        for (int x = 0; x < 8; ++x)
        {
            const unsigned v = kBayer8[y][x];
            if (v > 63u || seen[v])
                return false;
            seen[v] = true;
        }
    }
    return true;
}

static_assert(bayerCovers0Through63(), "Bayer matrix must be a permutation of 0..63");

} // namespace

namespace mviewer::core
{

std::uint8_t quantize16To8(std::uint16_t value, int x, int y) noexcept
{
    const unsigned sample = value;
    const unsigned base = sample / 257u;
    const unsigned rem = sample % 257u;
    // Exact quanta, including 0 and 65535 (255 * 257), stay flat. base == 255
    // also guards the +1 below; 65535 is the only sample that lands there.
    if (rem == 0u || base >= 255u)
        return static_cast<std::uint8_t>(base);

    // Two's-complement wrap: negative coordinates stay inside 0..7.
    const unsigned col = static_cast<unsigned>(x) & 7u;
    const unsigned row = static_cast<unsigned>(y) & 7u;
    const unsigned threshold = kBayer8[row][col];
    // Scale 0..63 into the open 257-wide remainder bucket (result is 0..253).
    const unsigned scaled = (threshold * 257u) / 64u;
    if (rem > scaled)
        return static_cast<std::uint8_t>(base + 1u);
    return static_cast<std::uint8_t>(base);
}

bool ditherQImage16ToRgb888(const QImage &src, QImage &out)
{
    try
    {
        if (src.isNull())
            return false;
        const QImage::Format fmt = src.format();
        const bool gray = fmt == QImage::Format_Grayscale16;
        const bool rgb64 = fmt == QImage::Format_RGBX64 || fmt == QImage::Format_RGBA64;
        if (!gray && !rgb64)
            return false;

        const int w = src.width();
        const int h = src.height();
        if (w <= 0 || h <= 0)
            return false;
        // x * 4 (64-bit pixel) and x * 3 (RGB888) must stay inside int.
        if (w > 0x1fffffff)
            return false;

        const qsizetype srcNeed = static_cast<qsizetype>(w) * (gray ? qsizetype{2} : qsizetype{8});
        if (src.bytesPerLine() < srcNeed)
            return false;

        QImage rgb(w, h, QImage::Format_RGB888);
        if (rgb.isNull() || rgb.bytesPerLine() < static_cast<qsizetype>(w) * 3)
            return false;

        for (int y = 0; y < h; ++y)
        {
            const auto *row = reinterpret_cast<const std::uint16_t *>(src.constScanLine(y));
            uchar *dst = rgb.scanLine(y);
            if (row == nullptr || dst == nullptr)
                return false;
            if (gray)
            {
                for (int x = 0; x < w; ++x)
                {
                    const std::uint8_t level = quantize16To8(row[x], x, y);
                    dst[x * 3 + 0] = level;
                    dst[x * 3 + 1] = level;
                    dst[x * 3 + 2] = level;
                }
            }
            else
            {
                for (int x = 0; x < w; ++x)
                {
                    const int i = x * 4;
                    dst[x * 3 + 0] = quantize16To8(row[i], x, y);
                    dst[x * 3 + 1] = quantize16To8(row[i + 1], x, y);
                    dst[x * 3 + 2] = quantize16To8(row[i + 2], x, y);
                }
            }
        }
        out.swap(rgb);
        return true;
    }
    catch (...)
    {
        return false;
    }
}

} // namespace mviewer::core
