#pragma once

#include "core/image/ImageFrame.h"

#include <cstdint>
#include <string>
#include <vector>

namespace mviewer::core
{

// Result of a compare algorithm run. Kept as plain POD so it can cross the
// plugin boundary without Qt types.
struct CompareAlgorithmResult
{
    // Human-readable summary (e.g. "PSNR = 42.1 dB").
    std::string summary;

    // Optional scalar metrics (name/value pairs).
    std::vector<std::pair<std::string, double>> metrics;

    // Optional heatmap / difference image (RGBA8, same size as reference).
    // Empty when the algorithm does not produce a visual overlay.
    std::vector<uint8_t> heatmapRgba;
    int heatmapWidth = 0;
    int heatmapHeight = 0;

    bool success = false;
    std::string error;
};

// ICompareAlgorithm — plugin interface for third-party compare algorithms.
//
// A compare algorithm takes a reference frame and one or more candidate
// frames and produces metrics + an optional heatmap. Built-in algorithms
// (PSNR, SSIM, DiffHeatmap) already live in core/compare; this interface
// lets third-party DLLs register additional algorithms without recompiling
// the host.
//
// Plugin C exports (mirrors Analyzer/Decoder/Exporter pattern):
//   extern "C" ICompareAlgorithm *createCompareAlgorithm();
//   extern "C" void destroyCompareAlgorithm(ICompareAlgorithm *);
//   extern "C" const char *pluginName();
//   extern "C" const PluginABI *mviewer_plugin_abi();
//
// Header is Qt-free. Implementations may use Qt in their .cpp.
class ICompareAlgorithm
{
  public:
    virtual ~ICompareAlgorithm() = default;

    // Stable unique id, e.g. "example.psnr_plus".
    virtual std::string name() const = 0;

    // Human-readable display name for UI menus.
    virtual std::string displayName() const = 0;

    // Run the algorithm. `reference` is the baseline; `candidates` are the
    // images being compared against it. Returns a result with metrics and
    // optional heatmap. Implementations must be thread-safe for concurrent
    // calls on independent instances.
    virtual CompareAlgorithmResult run(const ImageFrame &reference,
                                       const std::vector<const ImageFrame *> &candidates) = 0;
};

} // namespace mviewer::core
