#include "core/image/FrameSequenceDetail.h"

#include "core/image/MetadataReader.h"
#include "core/image/yuv/YuvFile.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <utility>

namespace mviewer::core
{
namespace
{

int clampFrameCount(int64_t count)
{
    if (count < 1)
        return 1;
    const int64_t limit = static_cast<int64_t>(std::numeric_limits<int>::max());
    if (count > limit)
        return std::numeric_limits<int>::max();
    return static_cast<int>(count);
}

FrameSequenceInfo sequenceFromOpened(const mviewer::core::yuv::OpenedYuvFile &opened)
{
    FrameSequenceInfo sequence;
    if (!opened.info.known)
        return sequence;
    sequence.valid = true;
    sequence.countKnown = true;
    sequence.animated = false;
    sequence.kind = FrameSequenceKind::Pages;
    sequence.frameCount = clampFrameCount(opened.info.frameCount);
    sequence.defaultFrame = 0;
    return sequence;
}

} // namespace

FrameSequenceInfo probeYuvSequence(const std::string &path)
{
    try
    {
        mviewer::core::yuv::OpenedYuvFile opened;
        if (!mviewer::core::yuv::openYuvFile(path, opened))
            return {};
        return sequenceFromOpened(opened);
    }
    catch (...)
    {
        return {};
    }
}

FrameDecodeResult decodeYuvSequenceFrame(const std::string &path, int frameIndex, int maxEdge)
{
    FrameDecodeResult result;
    try
    {
        mviewer::core::yuv::OpenedYuvFile opened;
        if (!mviewer::core::yuv::openYuvFile(path, opened) || !opened.info.known)
        {
            const std::string why =
                opened.info.why.empty() ? std::string("cannot open file") : opened.info.why;
            result.error = std::string("YUV parameters unknown: ") + why;
            return result;
        }
        result.sequence = sequenceFromOpened(opened);
        if (frameIndex < 0 || frameIndex >= result.sequence.frameCount)
        {
            result.error = "frame index out of range";
            return result;
        }

        // Scaled requests (thumbnails / filmstrip) sample the raw frame directly.
        std::string why;
        const int edge = maxEdge > 0 ? maxEdge : 0;
        result.pixels = mviewer::core::yuv::readYuvFrameImage(
            opened.info, static_cast<int64_t>(frameIndex), opened.read, edge,
            mviewer::core::yuv::ChromaUpsample::Nearest, &why);
        if (result.pixels.isNull() && !why.empty())
        {
            result.error = why;
            return result;
        }
        result.frame = {frameIndex, 0, opened.info.format.width, opened.info.format.height};
        mviewer::core::yuv::fillYuvMetadata(opened, path, frameIndex, result.metadata);
        const std::string suffix = mviewer::core::yuv::yuvCacheKeySuffix(path);
        result.identity.fileRevision = MetadataReader::key(path) + suffix;
        result.identity.frameIndex = frameIndex;
        result.identity.decodeVariant = std::max(0, maxEdge);
        result.ok = !result.pixels.isNull();
        if (!result.ok && result.error.empty())
            result.error = "YUV conversion failed";
        return result;
    }
    catch (...)
    {
        result.ok = false;
        if (result.error.empty())
            result.error = "YUV decode failed";
        return result;
    }
}

FrameInfo yuvFrameInfo(const std::string &path, int frameIndex)
{
    try
    {
        mviewer::core::yuv::OpenedYuvFile opened;
        if (!mviewer::core::yuv::openYuvFile(path, opened) || !opened.info.known)
            return {};
        const int count = clampFrameCount(opened.info.frameCount);
        if (frameIndex < 0 || frameIndex >= count)
            return {};
        return {frameIndex, 0, opened.info.format.width, opened.info.format.height};
    }
    catch (...)
    {
        return {};
    }
}

} // namespace mviewer::core
