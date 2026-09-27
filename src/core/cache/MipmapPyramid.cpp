#include "core/cache/MipmapPyramid.h"

#include <algorithm>
#include <cstdint>

namespace mviewer::cache
{

ImageData downscaleHalfBox(const ImageData &src)
{
    if (src.isNull())
        return ImageData{};
    const int sw = src.width;
    const int sh = src.height;
    const int dw = std::max(1, sw / 2);
    const int dh = std::max(1, sh / 2);
    if (dw == sw && dh == sh)
        return src;

    const int cpp = src.channelsPerPixel();
    ImageData dst = makeImageData(dw, dh, src.format);
    const ImageBuffer sv = src.view();
    const ImageBuffer dv = dst.view();

    for (int y = 0; y < dh; ++y)
    {
        const int y0 = y * 2;
        const int y1 = std::min(y0 + 1, sh - 1);
        uint8_t *dp = dv.data + static_cast<size_t>(y) * dv.stride();
        for (int x = 0; x < dw; ++x)
        {
            const int x0 = x * 2;
            const int x1 = std::min(x0 + 1, sw - 1);
            const uint8_t *p00 =
                sv.data + static_cast<size_t>(y0) * sv.stride() + static_cast<size_t>(x0) * cpp;
            const uint8_t *p01 =
                sv.data + static_cast<size_t>(y0) * sv.stride() + static_cast<size_t>(x1) * cpp;
            const uint8_t *p10 =
                sv.data + static_cast<size_t>(y1) * sv.stride() + static_cast<size_t>(x0) * cpp;
            const uint8_t *p11 =
                sv.data + static_cast<size_t>(y1) * sv.stride() + static_cast<size_t>(x1) * cpp;
            uint8_t *q = dp + static_cast<size_t>(x) * cpp;
            for (int c = 0; c < cpp; ++c)
            {
                const unsigned sum = static_cast<unsigned>(p00[c]) + static_cast<unsigned>(p01[c]) +
                                     static_cast<unsigned>(p10[c]) + static_cast<unsigned>(p11[c]);
                q[c] = static_cast<uint8_t>((sum + 2u) / 4u);
            }
        }
    }
    return dst;
}

std::vector<ImageData> buildMipChain(const ImageData &full, int minEdge)
{
    std::vector<ImageData> levels;
    if (full.isNull())
        return levels;
    const int stop = std::max(1, minEdge);
    levels.push_back(full); // lod 0 = full (shared buffer)
    ImageData cur = full;
    // Cap depth so a malformed tiny-step loop cannot explode memory.
    constexpr int kMaxLods = 16;
    for (int lod = 1; lod <= kMaxLods; ++lod)
    {
        if (imageMaxEdge(cur) <= stop)
            break;
        ImageData next = downscaleHalfBox(cur);
        if (next.isNull())
            break;
        if (next.width >= cur.width && next.height >= cur.height)
            break;
        levels.push_back(next);
        cur = std::move(next);
    }
    return levels;
}

} // namespace mviewer::cache
