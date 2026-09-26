#pragma once

#include "application/ImageLoadingService.h"

#include <string>
#include <vector>

namespace mviewer::ui
{

struct PairPrefetchEntry
{
    std::string path;
    mviewer::application::ImageLoadingService::AsyncRequestHandle handle;
};

// Paths for one navigation window starting at pairIndex + direction*navWindow.
// Empty when the window would not move or lies past the pool.
inline std::vector<std::string> neighborPairPaths(const std::vector<std::string> &pool,
                                                  int pairIndex, int navWindow, int direction)
{
    std::vector<std::string> out;
    if (pool.empty() || navWindow <= 0 || direction == 0)
        return out;
    int start = pairIndex + direction * navWindow;
    if (start < 0)
        start = 0;
    if (start >= static_cast<int>(pool.size()))
        return out;
    if (start == pairIndex)
        return out;
    out.reserve(static_cast<size_t>(navWindow));
    for (int i = 0; i < navWindow && start + i < static_cast<int>(pool.size()); ++i)
        out.push_back(pool[static_cast<size_t>(start + i)]);
    return out;
}

// True when finishLoad can keep existing RawImageView panes (same kept count).
inline bool canReuseComparePanes(int existingPaneCount, int keptFrameCount)
{
    return existingPaneCount > 0 && keptFrameCount > 0 && existingPaneCount == keptFrameCount;
}

} // namespace mviewer::ui
