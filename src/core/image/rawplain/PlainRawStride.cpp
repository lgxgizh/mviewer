#include "core/image/rawplain/PlainRawStride.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/rawplain/PlainRawPreview.h"
#include "core/image/rawplain/PlainRawRender.h"
#include "core/image/rawplain/PlainRawUnpack.h"

#include <cstdint>
#include <fstream>
#include <limits>
#include <string>
#include <utility>
#include <vector>

namespace mviewer::core::rawplain
{
namespace
{

struct PreviewGrid
{
    int width = 0;
    int height = 0;
    std::vector<int> srcX;
    std::vector<int> srcY;
};

// True when `src / preview` is at least 2, so a 2x grid of that axis fits.
bool shrinksByTwo(int src, int preview)
{
    if (preview < 1 || preview > (std::numeric_limits<int>::max() / 2))
    {
        return false;
    }
    return static_cast<std::int64_t>(preview) * 2 <= src;
}

// Even origin of Bayer quad `q` out of `quads`, spread across `src` samples.
int bayerOrigin(int q, int quads, int src)
{
    int origin = 0;
    if (quads > 0)
    {
        origin = static_cast<int>((static_cast<std::int64_t>(q) * src) / quads) & ~1;
    }
    const int last = (src - 2) & ~1;
    if (origin < 0 || last < 0)
    {
        return 0;
    }
    if (origin > last)
    {
        origin = last;
    }
    return origin;
}

void fillBayerAxis(int src, int quads, std::vector<int> &axis)
{
    axis.resize(static_cast<std::size_t>(quads) * 2U);
    for (int q = 0; q < quads; ++q)
    {
        const int origin = bayerOrigin(q, quads, src);
        axis[static_cast<std::size_t>(q) * 2U] = origin;
        axis[static_cast<std::size_t>(q) * 2U + 1U] = origin + 1;
    }
}

void fillMonoAxis(int src, int count, std::vector<int> &axis)
{
    axis.resize(static_cast<std::size_t>(count));
    for (int i = 0; i < count; ++i)
    {
        int pos = 0;
        if (count > 0)
        {
            pos = static_cast<int>((static_cast<std::int64_t>(i) * src) / count);
        }
        if (pos < 0)
        {
            pos = 0;
        }
        if (pos >= src)
        {
            pos = src - 1;
        }
        axis[static_cast<std::size_t>(i)] = pos;
    }
}

// Twice the preview on each axis: enough for the box average to anti-alias,
// far smaller than the source. Bayer quads start at (0, 0) on even origins so
// each reduced site keeps the source 2x2 phase. Returns false when the full
// payload path must be used instead (result stays byte-identical).
bool buildPreviewGrid(const Params &p, int maxEdge, int &outW, int &outH, PreviewGrid &grid)
{
    if (!detail::previewDimensions(p.width, p.height, maxEdge, outW, outH))
    {
        return false;
    }
    // A shrink below 2 on either axis, or a 2x grid that is the whole frame,
    // stays on readPlainRawSamples.
    if (!shrinksByTwo(p.width, outW) || !shrinksByTwo(p.height, outH))
    {
        return false;
    }
    const int gridW = outW * 2;
    const int gridH = outH * 2;
    if (gridW == p.width && gridH == p.height)
    {
        return false;
    }
    int gotW = 0;
    int gotH = 0;
    if (!detail::previewDimensions(gridW, gridH, maxEdge, gotW, gotH) || gotW != outW ||
        gotH != outH)
    {
        return false;
    }
    grid.width = gridW;
    grid.height = gridH;
    if (p.cfa == Cfa::Mono)
    {
        fillMonoAxis(p.width, gridW, grid.srcX);
        fillMonoAxis(p.height, gridH, grid.srcY);
        return true;
    }
    fillBayerAxis(p.width, outW, grid.srcX);
    fillBayerAxis(p.height, outH, grid.srcY);
    return true;
}

bool rowFileOffset(const Params &p, int y, std::int64_t &offset)
{
    const std::int64_t stride = effectiveRowStride(p);
    if (y < 0 || stride < 1 || p.headerOffset < 0)
    {
        return false;
    }
    std::int64_t body = 0;
    if (y > 0)
    {
        if (stride > std::numeric_limits<std::int64_t>::max() / y)
        {
            return false;
        }
        body = stride * static_cast<std::int64_t>(y);
    }
    if (p.headerOffset > std::numeric_limits<std::int64_t>::max() - body)
    {
        return false;
    }
    offset = p.headerOffset + body;
    const auto maxOff = static_cast<std::int64_t>(std::numeric_limits<std::streamoff>::max());
    return offset <= maxOff;
}

bool readAt(std::ifstream &file, std::int64_t offset, std::uint8_t *dst, std::size_t n)
{
    if (dst == nullptr || n == 0)
    {
        return false;
    }
    if (n > static_cast<std::size_t>(std::numeric_limits<std::streamsize>::max()))
    {
        return false;
    }
    file.clear();
    file.seekg(static_cast<std::streamoff>(offset), std::ios::beg);
    if (!file.good())
    {
        return false;
    }
    file.read(reinterpret_cast<char *>(dst), static_cast<std::streamsize>(n));
    return static_cast<std::size_t>(file.gcount()) == n;
}

bool sampleGridRow(const Params &p, const PreviewGrid &grid, const std::uint8_t *row,
                   std::size_t rowBytes, std::uint16_t *dst)
{
    for (int gx = 0; gx < grid.width; ++gx)
    {
        const int sx = grid.srcX[static_cast<std::size_t>(gx)];
        if (sx < 0 || sx >= p.width)
        {
            return false;
        }
        const int value = samplePlainRawRow(p, row, rowBytes, sx);
        if (value < 0)
        {
            return false;
        }
        dst[gx] = static_cast<std::uint16_t>(value);
    }
    return true;
}

// Reads only rows the grid uses. Each selected row is read in full (MIPI
// groups share bytes; other packings then keep only the grid columns).
bool readPreviewSamples(const std::string &path, const Params &p, const PreviewGrid &grid,
                        std::vector<std::uint16_t> &samples, std::string &why)
{
    samples.clear();
    if (static_cast<int>(grid.srcX.size()) != grid.width ||
        static_cast<int>(grid.srcY.size()) != grid.height || grid.width < 1 || grid.height < 1)
    {
        why = "unpack failed";
        return false;
    }
    const std::int64_t rowBytes = minRowBytes(p);
    const std::int64_t stride = effectiveRowStride(p);
    if (rowBytes < 1 || stride < rowBytes)
    {
        why = "invalid frame size";
        return false;
    }
    const auto streamMax = static_cast<std::int64_t>(std::numeric_limits<std::streamsize>::max());
    const auto sizeMax = static_cast<std::uint64_t>(std::numeric_limits<std::size_t>::max());
    if (rowBytes > streamMax || static_cast<std::uint64_t>(rowBytes) > sizeMax)
    {
        why = "frame is too large";
        return false;
    }
    std::ifstream file(mviewer::core::pathFromUtf8(path), std::ios::binary);
    if (!file.is_open())
    {
        why = "short read";
        return false;
    }
    const auto rowSize = static_cast<std::size_t>(rowBytes);
    std::vector<std::uint8_t> row(rowSize);
    const std::size_t n =
        static_cast<std::size_t>(grid.width) * static_cast<std::size_t>(grid.height);
    samples.assign(n, 0);
    int loaded = -1;
    for (int gy = 0; gy < grid.height; ++gy)
    {
        const int sy = grid.srcY[static_cast<std::size_t>(gy)];
        if (sy < 0 || sy >= p.height)
        {
            samples.clear();
            why = "unpack failed";
            return false;
        }
        if (sy != loaded)
        {
            std::int64_t offset = 0;
            if (!rowFileOffset(p, sy, offset))
            {
                samples.clear();
                why = "invalid frame size";
                return false;
            }
            if (!readAt(file, offset, row.data(), rowSize))
            {
                samples.clear();
                why = "short read";
                return false;
            }
            loaded = sy;
        }
        std::uint16_t *dst = samples.data() + static_cast<std::size_t>(gy) * grid.width;
        if (!sampleGridRow(p, grid, row.data(), rowSize, dst))
        {
            samples.clear();
            why = "unpack failed";
            return false;
        }
    }
    why.clear();
    return true;
}

bool renderPreviewGrid(const Params &source, std::vector<std::uint16_t> &&samples,
                       const PreviewGrid &grid, int maxEdge, int outW, int outH, ImageData &image,
                       std::string &why)
{
    // Layout fields are dropped: samples are already unpacked. Colour fields
    // (cfa, depth, black/white, wb, gamma, gain) stay on the source params.
    // Bit depth is the resolved value; a subset must not re-probe Lsb16.
    Params view = source;
    view.width = grid.width;
    view.height = grid.height;
    view.headerOffset = 0;
    view.rowStride = 0;
    image = renderPlainRawOwned(view, std::move(samples), maxEdge);
    if (image.isNull() || image.width != outW || image.height != outH)
    {
        image = ImageData{};
        why = "render failed";
        return false;
    }
    why.clear();
    return true;
}

} // namespace

StridePreviewStatus decodePlainRawStrided(const std::string &path, const PlainRawFileInfo &info,
                                          int maxEdge, ImageData &image, std::string &why)
{
    image = ImageData{};
    why.clear();
    try
    {
        if (!info.resolved.known)
        {
            return StridePreviewStatus::Unused;
        }
        int outW = 0;
        int outH = 0;
        PreviewGrid grid;
        if (!buildPreviewGrid(info.resolved.params, maxEdge, outW, outH, grid))
        {
            return StridePreviewStatus::Unused;
        }
        std::vector<std::uint16_t> samples;
        if (!readPreviewSamples(path, info.resolved.params, grid, samples, why))
        {
            image = ImageData{};
            return StridePreviewStatus::Failed;
        }
        if (!renderPreviewGrid(info.resolved.params, std::move(samples), grid, maxEdge, outW, outH,
                               image, why))
        {
            return StridePreviewStatus::Failed;
        }
        return StridePreviewStatus::Ready;
    }
    catch (...)
    {
        image = ImageData{};
        why = "exception while reading file";
        return StridePreviewStatus::Failed;
    }
}

} // namespace mviewer::core::rawplain
