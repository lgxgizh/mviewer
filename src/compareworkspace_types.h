#pragma once

#include "core/compare/DifferenceEngine.h"
#include "core/compare/Histogram.h"
#include "core/image/ImageBuffer.h"
#include "domain/Selection.h"

#include <QImage>

#include <cstdint>
#include <vector>

namespace mviewer::cw
{

// Value-only compare batch and navigation snapshots. CompareWorkspace aliases
// these names so existing CompareWorkspace::NavState spellings keep compiling.
// None of these types touch CompareWorkspace itself.

struct NavState
{
    bool blink = false;
    bool split = false;
    bool swipe = false;
    bool overlay = false;
    bool checker = false; // M23
    int checkerSize = 64; // M23
    bool diffHighlight = false;
    int diffGainIndex = 0;
    bool syncZoom = true;
    bool syncDrag = true;
    bool syncRotate = false;
    bool crosshair = false;
    bool pixelLink = false;
    bool filenameOverlay = true;
    int overlayMode = 0;
    int overlayAlpha = 45;
    uint8_t threshold = 0;
    int layoutIndex = 0;
    mviewer::domain::Selection roi;
    bool hasRoi = false;
};

struct InspectorSample
{
    int r = 0;
    int g = 0;
    int b = 0;
    bool valid = false;
};

struct DiffBatchResult
{
    uint64_t generation = 0;
    int baseIdx = 0;
    int targetIdx = -1;
    bool sizeMismatch = false; // first non-base cell differs in size
    bool aligned = false;
    int alignX = 0, alignY = 0;
    bool metricsValid = false;
    double psnr = 0.0;
    double ssim = 0.0;
    bool hasStats = false;
    DifferenceEngine::DiffStats stats;
    bool hasRoiStats = false;
    DifferenceEngine::DiffStats roiStats;
    double diffGain = 1.0;
    bool provisional = false; // live low-precision; metrics skipped
    uint8_t suggestedThreshold = 0;
    bool hasSuggestedThreshold = false;

    struct CellOverlay
    {
        int index = -1;
        bool sizeMismatch = false;
        QImage overlay;
        double opacity = 0.5;
        double psnr = 0.0;
        double ssim = 0.0;
        bool hasMetrics = false;
    };
    std::vector<CellOverlay> overlays;
};

struct DiffSources
{
    int targetIndex = -1;
    ImageData target;
    ImageData diff;
    bool sizeMismatch = false;
    double psnr = 0.0, ssim = 0.0;
    bool hasMetrics = false;
};

struct HistogramBatchResult
{
    uint64_t generation = 0;
    int paneCount = 0;
    bool updateMain = false;
    // M23: coherent title and histogram data delivered as one pair.
    bool roiEnabled = false;
    mviewer::domain::Selection roi;
    std::vector<mviewer::core::Histogram> main; // main surface, in main order

    struct CellHist
    {
        int index = -1;
        mviewer::core::Histogram hist;
    };
    std::vector<CellHist> panes; // pane overlay widgets keyed by index
};

struct HistIndexPlan
{
    bool updateMain = false;
    std::vector<int> mainIndices;
    std::vector<int> panes;
    std::vector<int> unionIdx;
};

} // namespace mviewer::cw
