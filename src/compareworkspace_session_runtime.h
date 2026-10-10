#pragma once

#include "compare_session_frame_pool.h"
#include "compareworkspace_display_types.h"

#include <cstdint>
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
    // Trailing quiet-period for the sharp display resample. Each wheel/pan
    // bumps this; only the timer that captured the latest value submits HQ.
    uint64_t lodDebounceEpoch = 0;
    // m_displayGen of the in-flight fast preview, or 0 when none is queued.
    uint64_t previewGeneration = 0;
    // True while queueFastPreview is inside scheduleDisplayMaterialization,
    // so setImage's scaleChanged cannot start another preview recursively.
    bool fastPreviewBusy = false;
    // 1 when the pane is showing a fast preview that still needs a sharp pass.
    std::vector<uint8_t> displayIsPreview;
};

} // namespace mviewer::ui
