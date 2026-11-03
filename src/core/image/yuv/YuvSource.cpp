#include "core/image/yuv/YuvSource.h"

#include "core/image/yuv/YuvScaledRender.h"

#include <algorithm>
#include <limits>

namespace mviewer::core::yuv
{
namespace
{

using scaled_detail::outputEdge;
using scaled_detail::wrapRgb;

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

namespace
{

// Frame stepping reads one frame after another; reuse a per-thread buffer for
// typical frame sizes instead of allocating (and page-faulting) every time.
constexpr size_t kKeepFrameBytes = size_t{16} << 20;

// Reads the raw bytes of frame 'index' into 'buf' (exactly frameBytes).
bool readRawFrame(const YuvSourceInfo &info, int64_t index, const ReadAtFn &read,
                  std::vector<uint8_t> &buf, std::string *why)
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
    buf.resize(nbytes);
    const size_t got = read(offset, buf.data(), nbytes);
    if (got != nbytes)
    {
        setWhy(why, "short read");
        return false;
    }
    return true;
}

std::vector<uint8_t> &frameBuffer(const YuvSourceInfo &info, std::vector<uint8_t> &big)
{
    thread_local std::vector<uint8_t> scratch;
    const bool keep =
        info.frameBytes >= 0 && static_cast<uint64_t>(info.frameBytes) <= kKeepFrameBytes;
    return keep ? scratch : big;
}

} // namespace

bool readYuvFrame(const YuvSourceInfo &info, int64_t index, const ReadAtFn &read, YuvPlanes &out,
                  std::string *why)
{
    std::vector<uint8_t> big;
    std::vector<uint8_t> &buf = frameBuffer(info, big);
    if (!readRawFrame(info, index, read, buf, why))
        return false;
    return unpackYuvFrame(info.format, buf.data(), buf.size(), out, why);
}

ImageData readYuvFrameImage(const YuvSourceInfo &info, int64_t index, const ReadAtFn &read,
                            int maxEdge, ChromaUpsample up, std::string *why)
{
    const Format &f = info.format;
    const bool scaleDown = maxEdge > 0 && std::max(f.width, f.height) > maxEdge;
    if (!scaleDown)
    {
        YuvPlanes planes;
        if (!readYuvFrame(info, index, read, planes, why))
            return {};
        return renderYuvImage(planes, f, maxEdge, up);
    }
    std::vector<uint8_t> big;
    std::vector<uint8_t> &buf = frameBuffer(info, big);
    if (!readRawFrame(info, index, read, buf, why))
        return {};
    // Same checks unpackYuvFrame and renderYuvImage apply to the planes.
    const auto layout = computeFrameLayout(f, why);
    if (!layout)
        return {};
    if (buf.size() < static_cast<uint64_t>(layout->frameBytes))
    {
        setWhy(why, "frame data too short");
        return {};
    }
    if (!isValid(f) || f.bitDepth < 1 || f.bitDepth > 16)
        return {};
    const int outW = outputEdge(f.width, f.height, maxEdge, true);
    const int outH = outputEdge(f.width, f.height, maxEdge, false);
    if (outW < 1 || outH < 1)
        return {};
    return scaled_detail::renderScaledRaw(buf.data(), *layout, f, outW, outH, up);
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
    return scaled_detail::renderScaledPlanes(planes, f, outW, outH, up);
}

} // namespace mviewer::core::yuv
