#pragma once

// Internal decode entry for ImageRepository. Not part of the public
// FrameSequenceReader contract: callers that already probed pass that
// sequence through so load does not scan the same file again.

#include "core/image/FrameSequence.h"

namespace mviewer::core
{

FrameDecodeResult decodeKnownSequence(const std::string &path, const FrameSequenceInfo &sequence,
                                      int frameIndex, int maxEdge);

} // namespace mviewer::core
