#pragma once

#include "compare_session_frame_pool.h"
#include "compareworkspace_display_types.h"

#include <memory>
#include <vector>

namespace mviewer::ui
{

// Heap-owned Compare session state kept out of compareworkspace.h.
struct CompareSessionRuntime
{
    CompareSessionFramePool framePool;
    std::vector<int> frameIndices; // parallel to m_comparePaths
    std::vector<ComparePanePyramidSlot> panePyramids;
    // When true, next materialization batch prefers Decode priority.
    bool forceDecodePriority = false;
};

} // namespace mviewer::ui
