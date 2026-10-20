#pragma once

#include <algorithm>
#include <vector>

namespace mviewer::ui
{

// Pure bookkeeping for the compare display batch (latest-wins) so every pane
// that is still waiting for a real raster is carried into the next batch.
//
// scheduleDisplayMaterialization() cancels the in-flight batch and bumps the
// generation. Before this helper, the replacement batch only re-requested the
// caller's dirty panes plus panes with a null image, so a pane that showed a
// warm thumb / previous raster with softLoading=true could lose its only
// pending request (e.g. a single-pane fast preview or per-pane scaleChanged
// LOD refresh cancelled the all-pane batch). That pane then kept the "Loading"
// badge and the low-res raster forever.

// Merge the pane targeted by a debounced LOD refresh. -1 means "all panes".
// Two different panes inside one debounce window must not collapse onto the
// last one: with sync zoom every view emits scaleChanged(own index).
inline int mergeLodRefreshPane(bool pending, int current, int incoming)
{
    if (!pending)
        return incoming;
    if (current < 0 || incoming < 0 || current != incoming)
        return -1;
    return current;
}

// Pane list for a new display batch: every pane still waiting on a raster
// (blank or softLoading), every pane the cancelled batch had not delivered
// yet, then the caller's dirty panes. Order is stable, entries are unique.
inline std::vector<int> collectDisplayBatchPanes(int paneCount,
                                                 const std::vector<bool> &waitingForRaster,
                                                 const std::vector<int> &undelivered,
                                                 const std::vector<int> &dirty)
{
    std::vector<int> panes;
    if (paneCount <= 0)
        return panes;
    panes.reserve(static_cast<size_t>(paneCount));
    auto add = [&panes, paneCount](int idx)
    {
        if (idx < 0 || idx >= paneCount)
            return;
        if (std::find(panes.cbegin(), panes.cend(), idx) != panes.cend())
            return;
        panes.push_back(idx);
    };
    for (int i = 0; i < paneCount && i < static_cast<int>(waitingForRaster.size()); ++i)
        if (waitingForRaster[static_cast<size_t>(i)])
            add(i);
    for (int idx : undelivered)
        add(idx);
    for (int idx : dirty)
        add(idx);
    return panes;
}

// Whether a pane's next request should be the cheap provisional LOD.
// A pane that just got its cheap pass must be asked for the sharp plan even
// when softLoading is still set: when the cheap raster was skipped as blurrier
// than the warm stand-in, softLoading stays true, and asking for cheap again
// livelocked cheap -> skip -> "upgrade" (cheap) forever on the thumb.
inline bool displayRequestWantsCheap(bool imageNull, bool softLoading, bool sharpUpgrade)
{
    if (imageNull)
        return true;
    return softLoading && !sharpUpgrade;
}

} // namespace mviewer::ui
