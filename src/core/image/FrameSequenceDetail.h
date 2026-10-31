#pragma once

// Internal decode entry for ImageRepository. Not part of the public
// FrameSequenceReader contract: callers that already probed pass that
// sequence through so load does not scan the same file again.

#include "core/image/FrameSequence.h"

#include <cstdint>
#include <string>

namespace mviewer::core
{

FrameDecodeResult decodeKnownSequence(const std::string &path, const FrameSequenceInfo &sequence,
                                      int frameIndex, int maxEdge);

// FrameSequenceProbe.cpp: GIF/WebP "as fast as possible" delays become 100 ms.
int normalizedAnimationDelayMs(int delayMs);
// Sums every frame delay of a short animation. Returns false only when the
// probe was cancelled; an unreadable delay still leaves a best-effort total.
bool probeAnimationDurationMs(const std::string &path, int frameCount, int64_t &total);
// Header-only size of a still/page frame (orientation applied); {} on failure.
FrameInfo stillFrameInfo(const std::string &path, int frameIndex);

} // namespace mviewer::core
