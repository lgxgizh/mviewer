#pragma once
#include <vector>
#include <cstdint>

namespace mviewer::domain {

// Per-cell transform (when sync is disabled)
struct CellTransform {
    double scale = 1.0;
    double offsetX = 0.0;
    double offsetY = 0.0;
};

// Session-level sync state
enum class SyncMode : uint8_t { Off, Zoom, Drag, All };

// Compare session: holds all state for a multi-image comparison
struct CompareSession {
    static constexpr int MAX_IMAGES = 8;

    // Session state
    std::vector<std::string> imageIds;  // file paths for now; upgrade to ImageId later
    std::vector<CellTransform> cells;  // Per-cell independent transforms
    SyncMode syncMode = SyncMode::All;
    int blinkIndex = -1;  // -1 = no blink

    // Shared transform (when sync enabled)
    double sharedScale = 1.0;
    double sharedOffsetX = 0.0;
    double sharedOffsetY = 0.0;

    // Layout (computed from image count)
    int cols = 0, rows = 0;

    int imageCount() const { return static_cast<int>(imageIds.size()); }
    bool isValid() const { return imageCount() >= 2 && imageCount() <= MAX_IMAGES; }
};

} // namespace mviewer::domain
