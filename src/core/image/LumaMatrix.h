#pragma once

// 8-bit display-referred luma for a chosen YUV matrix. Kr/Kb come from
// lumaKrKb() (the weights makeYuvCoeffs uses). Rec.601 is lumaRec601_8(), so
// untagged RGB stays byte-identical. Other matrices use the same Q16 rounding;
// Kg is 65536 - Kr - Kb so a gray pixel maps to itself (the BT.2020 residue
// of independent rounding, 1 LSB, sits on G).

#include "core/image/ImageBuffer.h"
#include "core/image/yuv/YuvFormat.h"
#include "domain/Image.h"

#include <cstdint>
#include <string_view>

namespace mviewer
{

inline uint8_t lumaMatrix8(int r, int g, int b, mviewer::core::yuv::Matrix matrix) noexcept
{
    int y = 0;
    if (matrix == mviewer::core::yuv::Matrix::BT601)
    {
        y = lumaRec601_8(r, g, b);
    }
    else
    {
        const mviewer::core::yuv::LumaKrKb k = mviewer::core::yuv::lumaKrKb(matrix);
        const int kr = static_cast<int>(k.kr * 65536.0 + 0.5);
        const int kb = static_cast<int>(k.kb * 65536.0 + 0.5);
        const int kg = 65536 - kr - kb;
        y = (kr * r + kg * g + kb * b + 32768) >> 16;
    }
    if (y <= 0)
        return 0;
    if (y >= 255)
        return 255;
    return static_cast<uint8_t>(y);
}

// MViewer.Yuv.Matrix, exact key. Values go through parseMatrix (bt601 / bt709 /
// bt2020, plus that parser's aliases). Missing key, empty, or unknown → BT.601.
inline mviewer::core::yuv::Matrix yuvMatrixFromTag(std::string_view tag)
{
    using mviewer::core::yuv::Matrix;
    if (tag.empty())
        return Matrix::BT601;
    Matrix parsed = Matrix::BT601;
    if (!mviewer::core::yuv::parseMatrix(tag, parsed))
        return Matrix::BT601;
    return parsed;
}

inline mviewer::core::yuv::Matrix yuvMatrixFromMetadata(const mviewer::domain::ImageMetadata &meta)
{
    const auto it = meta.textKeys.find("MViewer.Yuv.Matrix");
    if (it == meta.textKeys.end())
        return mviewer::core::yuv::Matrix::BT601;
    return yuvMatrixFromTag(it->second);
}

// meta wins when non-null. Otherwise the display raster's text tag (empty → 601).
inline mviewer::core::yuv::Matrix
yuvMatrixFromMetadataOrTag(const mviewer::domain::ImageMetadata *meta, std::string_view tag)
{
    if (meta != nullptr)
        return yuvMatrixFromMetadata(*meta);
    return yuvMatrixFromTag(tag);
}

// Folds the matrix into a QImage cache key. cacheKey() is a small monotonic id,
// so the top bits are free and a tagged Channel-Y raster is not reused as 601.
inline std::int64_t lumaOverlayCacheKey(std::int64_t imageKey,
                                        mviewer::core::yuv::Matrix matrix) noexcept
{
    const auto tag = static_cast<std::uint64_t>(static_cast<unsigned>(matrix)) << 60;
    return imageKey ^ static_cast<std::int64_t>(tag);
}

} // namespace mviewer
