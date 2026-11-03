#include "core/image/yuv/YuvSource.h"

#include <algorithm>
#include <limits>

namespace mviewer::core::yuv
{
namespace
{

void setWhy(std::string *why, const std::string &text)
{
    if (why != nullptr)
    {
        *why = text;
    }
}

bool endsWithY4m(std::string_view fileName)
{
    constexpr std::string_view kExt = ".y4m";
    if (fileName.size() < kExt.size())
    {
        return false;
    }
    const std::string_view tail = fileName.substr(fileName.size() - kExt.size());
    for (size_t i = 0; i < kExt.size(); ++i)
    {
        unsigned char c = static_cast<unsigned char>(tail[i]);
        if (c >= 'A' && c <= 'Z')
        {
            c = static_cast<unsigned char>(c - 'A' + 'a');
        }
        if (static_cast<char>(c) != kExt[i])
        {
            return false;
        }
    }
    return true;
}

void applyRawPayload(YuvSourceInfo &info, int64_t fileSize)
{
    std::string layoutWhy;
    const std::optional<FrameLayout> layout = computeFrameLayout(info.format, &layoutWhy);
    if (!layout || layout->frameBytes <= 0)
    {
        info.known = false;
        info.frameBytes = 0;
        info.frameCount = 0;
        info.trailingBytes = 0;
        if (info.why.empty())
        {
            info.why = layoutWhy.empty() ? "invalid frame layout" : layoutWhy;
        }
        return;
    }
    info.frameBytes = layout->frameBytes;
    const FrameCount count = computeFrameCount(fileSize, 0, 0, info.frameBytes);
    info.frameCount = count.frames;
    info.trailingBytes = count.trailingBytes;
    if (info.frameCount <= 0)
    {
        info.known = false;
        info.frameCount = 0;
        info.why = "file is smaller than one frame";
    }
}

int outputEdge(int srcW, int srcH, int maxEdge, bool horizontal)
{
    const int src = horizontal ? srcW : srcH;
    const int other = horizontal ? srcH : srcW;
    if (maxEdge <= 0 || src <= 0 || other <= 0)
    {
        return std::max(src, 0);
    }
    const int longest = std::max(srcW, srcH);
    if (longest <= maxEdge)
    {
        return src;
    }
    if ((horizontal && srcW >= srcH) || (!horizontal && srcH > srcW))
    {
        return maxEdge;
    }
    const int64_t scaled = (static_cast<int64_t>(src) * static_cast<int64_t>(maxEdge)) / other;
    if (scaled < 1)
    {
        return 1;
    }
    if (scaled > static_cast<int64_t>(std::numeric_limits<int>::max()))
    {
        return std::numeric_limits<int>::max();
    }
    return static_cast<int>(scaled);
}

int sampleCoord(int outIndex, int outCount, int srcCount)
{
    if (srcCount <= 1 || outCount <= 1)
    {
        return 0;
    }
    if (outIndex <= 0)
    {
        return 0;
    }
    if (outIndex >= outCount)
    {
        return srcCount - 1;
    }
    const int64_t num = static_cast<int64_t>(outIndex) * static_cast<int64_t>(srcCount);
    int64_t coord = num / outCount;
    if (coord >= srcCount)
    {
        coord = srcCount - 1;
    }
    if (coord < 0)
    {
        coord = 0;
    }
    return static_cast<int>(coord);
}

struct ChromaTap
{
    int i0 = 0;
    int i1 = 0;
    int weight1 = 0; // 0..256
};

ChromaTap mapChroma(int luma, int shift, int planeSize, bool center, bool bilinear)
{
    ChromaTap tap;
    if (planeSize <= 0)
    {
        return tap;
    }
    if (shift <= 0)
    {
        int idx = luma;
        if (idx < 0)
        {
            idx = 0;
        }
        if (idx >= planeSize)
        {
            idx = planeSize - 1;
        }
        tap.i0 = idx;
        tap.i1 = idx;
        return tap;
    }
    const int scale = 1 << shift;
    int adjusted = luma;
    if (center)
    {
        adjusted = luma - (scale / 2);
    }
    if (!bilinear)
    {
        int idx = adjusted < 0 ? 0 : (adjusted >> shift);
        if (idx >= planeSize)
        {
            idx = planeSize - 1;
        }
        tap.i0 = idx;
        tap.i1 = idx;
        return tap;
    }
    if (adjusted < 0)
    {
        adjusted = 0;
    }
    int base = adjusted >> shift;
    const int frac = adjusted & (scale - 1);
    if (base >= planeSize)
    {
        base = planeSize - 1;
    }
    int next = base + 1;
    if (next >= planeSize)
    {
        next = planeSize - 1;
    }
    tap.i0 = base;
    tap.i1 = next;
    tap.weight1 = (frac * 256) / scale;
    return tap;
}

int16_t mixChroma(const std::vector<uint16_t> &plane, int planeW, int planeH, int sx, int sy,
                  int shiftX, int shiftY, bool centerX, bool centerY, bool bilinear, int bitDepth)
{
    const ChromaTap tx = mapChroma(sx, shiftX, planeW, centerX, bilinear);
    const ChromaTap ty = mapChroma(sy, shiftY, planeH, centerY, bilinear);
    const int s00 = sampleAt(plane, planeW, planeH, tx.i0, ty.i0);
    const int s10 = sampleAt(plane, planeW, planeH, tx.i1, ty.i0);
    const int s01 = sampleAt(plane, planeW, planeH, tx.i0, ty.i1);
    const int s11 = sampleAt(plane, planeW, planeH, tx.i1, ty.i1);
    const int top = s00 + ((s10 - s00) * tx.weight1) / 256;
    const int bot = s01 + ((s11 - s01) * tx.weight1) / 256;
    const int mixed = top + ((bot - top) * ty.weight1) / 256;
    int clamped = mixed;
    if (clamped < 0)
    {
        clamped = 0;
    }
    const int maxCode = (bitDepth >= 16) ? 65535 : ((1 << bitDepth) - 1);
    if (clamped > maxCode)
    {
        clamped = maxCode;
    }
    return toTwelveBit(static_cast<uint16_t>(clamped), bitDepth);
}

bool planesMatch(const YuvPlanes &planes, const Format &f)
{
    if (!isValid(f) || planes.width != f.width || planes.height != f.height ||
        planes.bitDepth != f.bitDepth || f.bitDepth < 1 || f.bitDepth > 16)
    {
        return false;
    }
    int64_t yCount = 0;
    if (!checkedMul(f.width, f.height, yCount) || yCount <= 0)
    {
        return false;
    }
    if (static_cast<int64_t>(planes.y.size()) < yCount)
    {
        return false;
    }
    if (f.subsampling == Subsampling::S400)
    {
        return planes.chromaWidth == 0 && planes.chromaHeight == 0;
    }
    const int cw = chromaWidth(f);
    const int ch = chromaHeight(f);
    if (planes.chromaWidth != cw || planes.chromaHeight != ch || cw <= 0 || ch <= 0)
    {
        return false;
    }
    int64_t cCount = 0;
    if (!checkedMul(cw, ch, cCount) || cCount <= 0)
    {
        return false;
    }
    return static_cast<int64_t>(planes.u.size()) >= cCount &&
           static_cast<int64_t>(planes.v.size()) >= cCount;
}

ImageData wrapRgb(int width, int height, std::vector<uint8_t> &&rgb)
{
    ImageData image;
    if (width <= 0 || height <= 0 || rgb.empty())
    {
        return image;
    }
    image.width = width;
    image.height = height;
    image.format = PixelFormat::RGB24;
    image.buffer = std::make_shared<std::vector<uint8_t>>(std::move(rgb));
    return image;
}

ImageData renderScaled(const YuvPlanes &planes, const Format &f, int outW, int outH,
                       ChromaUpsample up)
{
    int64_t pixels = 0;
    int64_t bytes64 = 0;
    if (!checkedMul(outW, outH, pixels) || !checkedMul(pixels, 3, bytes64) || bytes64 <= 0)
    {
        return {};
    }
    if (static_cast<uint64_t>(bytes64) > static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        return {};
    }
    std::vector<uint8_t> rgb(static_cast<size_t>(bytes64));
    const YuvCoeffs coeffs = makeYuvCoeffs(f.matrix, f.range, f.bitDepth);
    const bool gray = f.subsampling == Subsampling::S400;
    const bool bilinear = up == ChromaUpsample::Bilinear;
    const bool centerX = f.siting == ChromaSiting::Center;
    const bool centerY = f.siting == ChromaSiting::Center;
    const int shiftX = chromaShiftX(f.subsampling);
    const int shiftY = chromaShiftY(f.subsampling);
    std::vector<int16_t> y12(static_cast<size_t>(outW));
    std::vector<int16_t> u12(static_cast<size_t>(gray ? 0 : outW));
    std::vector<int16_t> v12(static_cast<size_t>(gray ? 0 : outW));
    const size_t stride = static_cast<size_t>(outW) * 3u;
    for (int oy = 0; oy < outH; ++oy)
    {
        const int sy = sampleCoord(oy, outH, planes.height);
        for (int ox = 0; ox < outW; ++ox)
        {
            const int sx = sampleCoord(ox, outW, planes.width);
            const uint16_t ySample = sampleAt(planes.y, planes.width, planes.height, sx, sy);
            y12[static_cast<size_t>(ox)] = toTwelveBit(ySample, f.bitDepth);
            if (!gray)
            {
                u12[static_cast<size_t>(ox)] =
                    mixChroma(planes.u, planes.chromaWidth, planes.chromaHeight, sx, sy, shiftX,
                              shiftY, centerX, centerY, bilinear, f.bitDepth);
                v12[static_cast<size_t>(ox)] =
                    mixChroma(planes.v, planes.chromaWidth, planes.chromaHeight, sx, sy, shiftX,
                              shiftY, centerX, centerY, bilinear, f.bitDepth);
            }
        }
        convertRowScalar(y12.data(), gray ? nullptr : u12.data(), gray ? nullptr : v12.data(), outW,
                         coeffs, rgb.data() + static_cast<size_t>(oy) * stride);
    }
    return wrapRgb(outW, outH, std::move(rgb));
}

} // namespace

YuvParamStore &YuvParamStore::instance()
{
    static YuvParamStore store;
    return store;
}

void YuvParamStore::set(const std::string &key, const Format &f)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map[key] = f;
    ++m_revision;
}

std::optional<Format> YuvParamStore::get(const std::string &key) const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    const auto it = m_map.find(key);
    if (it == m_map.end())
    {
        return std::nullopt;
    }
    return it->second;
}

void YuvParamStore::clear(const std::string &key)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map.erase(key);
    ++m_revision;
}

void YuvParamStore::clearAll()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_map.clear();
    ++m_revision;
}

uint64_t YuvParamStore::revision() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_revision;
}

bool isY4mFileName(std::string_view fileName)
{
    return endsWithY4m(fileName);
}

YuvSourceInfo resolveYuvSource(std::string_view fileName, std::string_view folderName,
                               int64_t fileSize, const ReadAtFn &read,
                               const std::optional<Format> &saved)
{
    YuvSourceInfo info;
    info.isY4m = isY4mFileName(fileName);
    if (info.isY4m)
    {
        if (!read)
        {
            info.why = "invalid y4m header";
            return info;
        }
        std::vector<uint8_t> header(kMaxY4mHeaderBytes);
        const size_t got = read(0, header.data(), header.size());
        std::string parseWhy;
        const std::optional<Y4mInfo> parsed = parseY4mHeader(header.data(), got, &parseWhy);
        if (!parsed)
        {
            info.why = parseWhy.empty() ? "invalid y4m header" : parseWhy;
            return info;
        }
        info.source = ParamSource::Y4mHeader;
        info.format = parsed->format;
        info.fpsNum = parsed->fpsNum;
        info.fpsDen = parsed->fpsDen;
        info.dataOffset = parsed->headerBytes;
        const std::optional<FrameLayout> layout = computeFrameLayout(info.format);
        if (!layout || layout->frameBytes <= 0)
        {
            info.why = "invalid frame layout";
            return info;
        }
        info.frameBytes = layout->frameBytes;
        info.y4mFrameOffsets = indexY4mFrames(*parsed, fileSize, read);
        info.frameCount = static_cast<int64_t>(info.y4mFrameOffsets.size());
        if (info.frameCount <= 0)
        {
            info.known = false;
            info.why = "file is smaller than one frame";
            return info;
        }
        const int64_t last = info.y4mFrameOffsets.back();
        int64_t end = 0;
        if (checkedAdd(last, info.frameBytes, end) && fileSize > end)
        {
            info.trailingBytes = fileSize - end;
        }
        info.known = true;
        return info;
    }

    info.inference = inferYuvParams(fileName, folderName, fileSize);
    info.dataOffset = 0;
    if (saved.has_value() && isValid(*saved))
    {
        info.format = *saved;
        info.source = ParamSource::Saved;
        info.known = true;
    }
    else
    {
        info.format = info.inference.format;
        info.source = ParamSource::Inferred;
        info.known = info.inference.sizeKnown;
        if (!info.known)
        {
            info.why = "frame size is ambiguous or unknown";
        }
    }
    applyRawPayload(info, fileSize);
    return info;
}

int64_t yuvFrameOffset(const YuvSourceInfo &info, int64_t index)
{
    if (!info.known || index < 0 || info.frameBytes <= 0 || index >= info.frameCount)
    {
        return -1;
    }
    if (info.isY4m)
    {
        if (static_cast<uint64_t>(index) >= info.y4mFrameOffsets.size())
        {
            return -1;
        }
        return info.y4mFrameOffsets[static_cast<size_t>(index)];
    }
    int64_t delta = 0;
    int64_t offset = 0;
    if (!checkedMul(index, info.frameBytes, delta) || !checkedAdd(info.dataOffset, delta, offset))
    {
        return -1;
    }
    return offset;
}

bool readYuvFrame(const YuvSourceInfo &info, int64_t index, const ReadAtFn &read, YuvPlanes &out,
                  std::string *why)
{
    if (!info.known)
    {
        setWhy(why, "format is not known");
        return false;
    }
    const int64_t offset = yuvFrameOffset(info, index);
    if (offset < 0)
    {
        setWhy(why, "frame index out of range");
        return false;
    }
    if (!read)
    {
        setWhy(why, "short read");
        return false;
    }
    if (static_cast<uint64_t>(info.frameBytes) >
        static_cast<uint64_t>(std::numeric_limits<size_t>::max()))
    {
        setWhy(why, "frame is too large");
        return false;
    }
    const size_t nbytes = static_cast<size_t>(info.frameBytes);
    std::vector<uint8_t> buf(nbytes);
    const size_t got = read(offset, buf.data(), nbytes);
    if (got != nbytes)
    {
        setWhy(why, "short read");
        return false;
    }
    return unpackYuvFrame(info.format, buf.data(), buf.size(), out, why);
}

ImageData renderYuvImage(const YuvPlanes &planes, const Format &f, int maxEdge, ChromaUpsample up)
{
    if (!planesMatch(planes, f))
    {
        return {};
    }
    const bool scaleDown = maxEdge > 0 && std::max(planes.width, planes.height) > maxEdge;
    if (!scaleDown)
    {
        std::vector<uint8_t> rgb = toRgb24(planes, f, up);
        if (rgb.empty())
        {
            return {};
        }
        return wrapRgb(planes.width, planes.height, std::move(rgb));
    }
    const int outW = outputEdge(planes.width, planes.height, maxEdge, true);
    const int outH = outputEdge(planes.width, planes.height, maxEdge, false);
    if (outW < 1 || outH < 1)
    {
        return {};
    }
    return renderScaled(planes, f, outW, outH, up);
}

} // namespace mviewer::core::yuv
