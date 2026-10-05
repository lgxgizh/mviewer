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
    // Panes whose view must show the engine cell scale/pan after setImage().
    // Swap exchanges rasters; resetFit would otherwise drop the swapped view.
    std::vector<int> reapplyCellTransform;
    // Bumped when a swap commits per-pane scale/pan. A fit queued earlier
    // sees the mismatch and does not overwrite those values.
    uint64_t cellTransformEpoch = 0;
    // schedulePostLayoutFit coalesces while one callback is queued. Set when
    // a newer request arrives, so an epoch-superseded callback can requeue it.
    bool postLayoutFitAgain = false;
};

} // namespace mviewer::ui
