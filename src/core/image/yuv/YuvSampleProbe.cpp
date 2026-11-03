#include "core/image/yuv/YuvSampleProbe.h"

#include "core/filesystem/Utf8Path.h"
#include "core/image/yuv/YuvFile.h"
#include "core/image/yuv/YuvSource.h"

#include <filesystem>
#include <system_error>
#include <utility>

namespace mviewer::core::yuv
{
namespace
{

bool planeCovers(const std::vector<uint16_t> &plane, int width, int height)
{
    if (width <= 0 || height <= 0)
        return false;
    const int64_t need = static_cast<int64_t>(width) * static_cast<int64_t>(height);
    return need > 0 && static_cast<int64_t>(plane.size()) >= need;
}

// -1 when the file cannot be sized. Cheap: no open, no y4m re-index.
int64_t currentFileSize(const std::string &path)
{
    std::error_code ec;
    const auto size = std::filesystem::file_size(mviewer::core::pathFromUtf8(path), ec);
    if (ec)
        return -1;
    return static_cast<int64_t>(size);
}

int clampIndex(int value, int limit)
{
    if (value < 0)
        return 0;
    if (limit <= 0)
        return 0;
    if (value >= limit)
        return limit - 1;
    return value;
}

} // namespace

YuvSample sampleYuvPlanes(const YuvPlanes &planes, const Format &f, int x, int y)
{
    YuvSample sample;
    sample.x = x;
    sample.y = y;
    sample.bitDepth = planes.bitDepth > 0 ? planes.bitDepth : f.bitDepth;
    sample.hasChroma = f.subsampling != Subsampling::S400;

    if (x < 0 || y < 0 || x >= planes.width || y >= planes.height)
        return sample;
    if (!planeCovers(planes.y, planes.width, planes.height))
        return sample;

    if (sample.hasChroma)
    {
        if (!planeCovers(planes.u, planes.chromaWidth, planes.chromaHeight) ||
            !planeCovers(planes.v, planes.chromaWidth, planes.chromaHeight))
            return sample;
        const int shiftX = chromaShiftX(f.subsampling);
        const int shiftY = chromaShiftY(f.subsampling);
        const int cx = clampIndex(shiftX >= 0 ? (x >> shiftX) : x, planes.chromaWidth);
        const int cy = clampIndex(shiftY >= 0 ? (y >> shiftY) : y, planes.chromaHeight);
        sample.chromaX = cx;
        sample.chromaY = cy;
        sample.cb = sampleAt(planes.u, planes.chromaWidth, planes.chromaHeight, cx, cy);
        sample.cr = sampleAt(planes.v, planes.chromaWidth, planes.chromaHeight, cx, cy);
    }

    sample.luma = sampleAt(planes.y, planes.width, planes.height, x, y);
    sample.valid = true;
    return sample;
}

std::string formatYuvSample(const YuvSample &s)
{
    if (!s.valid)
        return {};
    std::string text = "Y=" + std::to_string(s.luma);
    if (s.hasChroma)
    {
        text += " U=" + std::to_string(s.cb);
        text += " V=" + std::to_string(s.cr);
        text += " (";
        text += std::to_string(s.bitDepth);
        text += "-bit, chroma ";
        text += std::to_string(s.chromaX);
        text += ",";
        text += std::to_string(s.chromaY);
        text += ")";
    }
    else
    {
        text += " (";
        text += std::to_string(s.bitDepth);
        text += "-bit)";
    }
    return text;
}

double normalizedSample(uint16_t value, int bitDepth)
{
    if (bitDepth <= 0 || bitDepth > 16)
        return 0.0;
    const int maxCode = (1 << bitDepth) - 1;
    if (maxCode <= 0)
        return 0.0;
    return static_cast<double>(value) / static_cast<double>(maxCode);
}

YuvSampleProbe &YuvSampleProbe::instance()
{
    static YuvSampleProbe probe;
    return probe;
}

bool YuvSampleProbe::cacheHit(const std::string &path, int64_t frameIndex,
                              const std::string &suffix, uint64_t revision, int64_t fileSize) const
{
    return m_cache.present && m_cache.path == path && m_cache.frameIndex == frameIndex &&
           m_cache.suffix == suffix && m_cache.revision == revision && m_cache.fileSize == fileSize;
}

YuvSample YuvSampleProbe::sample(const std::string &path, int64_t frameIndex, int x, int y)
{
    std::lock_guard<std::mutex> lock(m_mutex);
    YuvSample invalid;
    invalid.x = x;
    invalid.y = y;
    invalid.frameIndex = frameIndex;

    const std::string suffix = yuvCacheKeySuffix(path);
    const uint64_t revision = YuvParamStore::instance().revision();

    // Mouse moves hit the cached frame without reopening (or re-indexing a y4m).
    const int64_t fileSize = currentFileSize(path);
    if (frameIndex < 0 || fileSize < 0)
        return invalid;
    if (!cacheHit(path, frameIndex, suffix, revision, fileSize))
    {
        OpenedYuvFile opened;
        if (!openYuvFile(path, opened) || !opened.info.known ||
            frameIndex >= opened.info.frameCount)
            return invalid;
        YuvPlanes planes;
        ++m_reads;
        if (!readYuvFrame(opened.info, frameIndex, opened.read, planes, nullptr))
        {
            m_cache.present = false;
            return invalid;
        }
        m_cache.present = true;
        m_cache.path = path;
        m_cache.frameIndex = frameIndex;
        m_cache.suffix = suffix;
        m_cache.revision = revision;
        m_cache.fileSize = fileSize;
        m_cache.format = opened.info.format;
        m_cache.planes = std::move(planes);
    }

    YuvSample sample = sampleYuvPlanes(m_cache.planes, m_cache.format, x, y);
    sample.frameIndex = frameIndex;
    return sample;
}

void YuvSampleProbe::clear()
{
    std::lock_guard<std::mutex> lock(m_mutex);
    m_cache = Cache{};
}

uint64_t YuvSampleProbe::readCount() const
{
    std::lock_guard<std::mutex> lock(m_mutex);
    return m_reads;
}

} // namespace mviewer::core::yuv
