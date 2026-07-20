#pragma once
#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::domain
{

// Per-cell transform (when sync is disabled)
struct CellTransform
{
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
};

// Session-level sync state
enum class SyncMode : uint8_t
{
    Off,
    Zoom,
    Drag,
    All
};

// Viewport state for a compare workspace.
struct Viewport
{
    int width = 0;
    int height = 0;
    int cellW = 0;
    int cellH = 0;
    int cols = 0;
    int rows = 0;
};

// Selection state for compare.
struct CompareSelection
{
    int x = 0, y = 0, w = 0, h = 0;
    bool active = false;
    bool synced = false; // sync selection across cells
};

// CompareSession: the SOLE owner of all comparison state.
// Workspace reads from it; never owns state.
struct CompareSession
{
    static constexpr int MAX_IMAGES = 8;

    std::vector<std::string> imageIds;
    std::vector<CellTransform> cells;
    SyncMode syncMode = SyncMode::All;
    int blinkIndex = -1;

    double sharedScale = 1.0;
    double sharedOffsetX = 0.0;
    double sharedOffsetY = 0.0;

    int cols = 0, rows = 0;

    Viewport viewport;
    CompareSelection selection;

    int imageCount() const
    {
        return static_cast<int>(imageIds.size());
    }
    bool isValid() const
    {
        return imageCount() >= 2 && imageCount() <= MAX_IMAGES;
    }

    // Derived: true when a session is in "comparing" mode (≥2 images loaded).
    bool isComparing() const
    {
        return isValid();
    }
    bool isBlinking() const
    {
        return blinkIndex >= 0 && blinkIndex < imageCount();
    }
    bool isSyncOn() const
    {
        return syncMode != SyncMode::Off;
    }
};

} // namespace mviewer::domain
