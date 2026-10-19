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
    // M57: frame/page identity aligned with imageIds. Legacy sessions omit
    // this array and therefore restore every source at frame/page zero.
    std::vector<int> frameIndices;
    std::vector<CellTransform> cells;
    SyncMode syncMode = SyncMode::All;
    int blinkIndex = -1;

    double sharedScale = 1.0;
    double sharedOffsetX = 0.0;
    double sharedOffsetY = 0.0;

    int cols = 0, rows = 0;

    Viewport viewport;
    CompareSelection selection;

    // M15 P0#1 (review): additional UI state that must survive a save/close/reopen
    // cycle so the Compare view is fully restored, not just its geometry.
    uint8_t threshold = 0;         // HeatMap / Diff threshold (0-255)
    int blinkIntervalMs = 500;     // Blink interval (was hardcoded 500; now persistable)
    bool sidePanelVisible = false; // Inspector panel visibility
    int layoutIndex = 0;           // Layout combo selection (0=auto,1=1-col,2=2-col,3=3-col,4=4-col,5=single-row,6=custom)
    int customColumns = 2;         // Custom layout column count (rows derived from image count)

    // H5: "uniform pixel scale" — force every pane to the same zoom so images of
    // different resolutions line up 1:1 in pixel scale (not fitted independently).
    bool uniformScale = false;

    int imageCount() const noexcept
    {
        return static_cast<int>(imageIds.size());
    }
    bool isValid() const noexcept
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
