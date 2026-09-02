#pragma once

#include "core/image/ImageBuffer.h"
#include "core/image/ImageStats.h"
#include "core/image/SourceImage.h"
#include "domain/Image.h"
#include "domain/Selection.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::ui
{

enum class ROIMeasurementState
{
    Idle,
    Measuring,
    Ready,
    Unsupported,
    Failed,
    Backpressured
};

enum class ROIPaneState
{
    Ready,
    Unsupported,
    Failed,
    Cancelled
};

struct ROIInput
{
    ImageData pixels;
    mviewer::domain::ImageMetadata metadata;
    std::string path;
};

struct ROIPaneMeasurement
{
    mviewer::core::ROIChannelStats stats;
    ROIPaneState state = ROIPaneState::Failed;
    std::string reason;
    mviewer::core::SourceDecodePath decodePath = mviewer::core::SourceDecodePath::ProbeMetadata;
};

struct ROIStatsBatchResult
{
    uint64_t generation = 0;
    int paneCount = 0;
    bool linked = false;
    mviewer::domain::Selection roi;
    std::vector<ROIPaneMeasurement> panes;
};

} // namespace mviewer::ui
