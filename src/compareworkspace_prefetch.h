#pragma once

namespace mviewer::ui
{

// True when finishLoad can keep existing RawImageView panes (same kept count).
inline bool canReuseComparePanes(int existingPaneCount, int keptFrameCount)
{
    return existingPaneCount > 0 && keptFrameCount > 0 && existingPaneCount == keptFrameCount;
}

} // namespace mviewer::ui
