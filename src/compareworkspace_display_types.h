#pragma once

#include "core/image/ImageBuffer.h"
#include "domain/Image.h"

#include <QImage>
#include <QRect>
#include <QSize>
#include <QString>

#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::ui
{

// Value-only display materialization types (kept out of compareworkspace.h for
// the Strict 800-line gate). CompareWorkspace aliases these as nested names.

struct CompareDisplayRequest
{
    QSize target;
    // Displayed/oriented source rectangle covered by this request. A
    // full-frame LOD uses the complete source geometry.
    QRect sourceRect;
    bool region = false;
    bool provisional = false; // cheap first paint; upgrade after delivery
};

struct CompareDisplayBatchResult
{
    uint64_t generation = 0;
    int paneCount = 0;
    bool provisional = false;

    struct CellImage
    {
        int index = -1;
        QImage image;
        QSize sourceSize;
        QRect sourceRect;
        // Terminal bounded-display failure. The last valid image remains
        // intact when one exists; an initial failure is surfaced in the
        // pane caption instead of leaving an unexplained blank cell.
        QString errorText;
    };
    std::vector<CellImage> cells;
};

struct CompareSourceDisplayResult
{
    ImageData pixels;
    mviewer::domain::ImageMetadata metadata;
    QSize sourceSize;
    QRect coveredRect;
    QString errorText;
};

// Per-pane pyramid cache entry (coarse→fine rasters retained until superseded).
struct ComparePanePyramidSlot
{
    std::string path;
    std::vector<CompareDisplayRequest> levels; // parallel to images
    std::vector<QImage> images;
    QSize sourceSize;
    bool haveFull = false;
};

} // namespace mviewer::ui
