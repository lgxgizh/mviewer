#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <cstdint>
#include <string>

namespace mviewer::core
{

// A multi-frame source keeps its container semantics separate from its frame
// index. GIF/WebP are time sequences; TIFF pages are navigable pages.
enum class FrameSequenceKind : uint8_t
{
    Static,
    Animation,
    Pages
};

// FileRevision is deliberately represented by the existing repository key.
// Adding the frame index here prevents a frame-0 cache hit from satisfying a
// request for frame 1 while preserving the M56 size/mtime identity.
struct FrameIdentity
{
    std::string fileRevision;
    int frameIndex = 0;
    int decodeVariant = 0; // 0 = native/full, positive = bounded max-edge variant

    bool operator==(const FrameIdentity &) const = default;

    std::string cacheKey() const;
};

struct FrameSequenceInfo
{
    bool valid = false;
    bool countKnown = true;
    bool animated = false;
    FrameSequenceKind kind = FrameSequenceKind::Static;
    int frameCount = 1;
    int defaultFrame = 0;
    int loopCount = -1; // -1 means unknown/infinite according to the plugin
    int64_t totalDurationMs = 0;
    bool durationKnown = false;
};

struct FrameInfo
{
    int index = 0;
    int durationMs = 0;
    int width = 0;
    int height = 0;
};

struct FrameDecodeResult
{
    bool ok = false;
    FrameIdentity identity;
    FrameSequenceInfo sequence;
    FrameInfo frame;
    ImageData pixels;
    mviewer::domain::ImageMetadata metadata;
    std::string error;
};

// Core-owned multi-frame capability. The public contract is Qt-free; the
// implementation uses QImageReader in this core translation unit only.
class FrameSequenceReader
{
  public:
    static FrameSequenceInfo probe(const std::string &path);

    // Named capability operations used by application/UI callers. The short
    // aliases below keep the implementation surface small while making the
    // sequence contract explicit at call sites.
    static FrameSequenceInfo probeSequence(const std::string &path)
    {
        return probe(path);
    }

    // maxEdge <= 0 requests the full current frame. A positive maxEdge is a
    // bounded decode variant and never enumerates or materializes all frames.
    static FrameDecodeResult decode(const std::string &path, int frameIndex,
                                    int maxEdge = 0);

    static FrameDecodeResult decodeFrame(const std::string &path, int frameIndex)
    {
        return decode(path, frameIndex, 0);
    }

    static FrameDecodeResult decodeFrameScaled(const std::string &path, int frameIndex,
                                               int maxEdge)
    {
        return decode(path, frameIndex, maxEdge);
    }

    static FrameDecodeResult decodeFull(const std::string &path, int frameIndex = 0)
    {
        return decode(path, frameIndex, 0);
    }

    static FrameInfo frameInfo(const std::string &path, int frameIndex);
    static bool isSequencePath(const std::string &path);
};

} // namespace mviewer::core
