#pragma once

#include <cstdint>
#include <string>
#include <vector>

// Self-describing capability flags for analyzers.
// Agents and UI query these to determine which analyzer fits a task.
enum class AnalyzerCapability : uint32_t
{
    None = 0,
    SingleImage = 1 << 0,        // operates on a single frame at a time
    MultiImage = 1 << 1,         // compares two or more frames
    RegionOfInterest = 1 << 2,   // supports analyzeRegion() on a Selection
    Streaming = 1 << 3,          // progressive analysis with onProgress
    GPU = 1 << 4,                // offloads to GPU if available
    HistogramOutput = 1 << 5,    // output includes histogram data
    StatsOutput = 1 << 6,        // output includes ImageStats (means, etc.)
    QualityMetric = 1 << 7,      // output is a computed quality score (PSNR/SSIM)
    DifferenceOutput = 1 << 8,   // output is an ImageData (heatmap/difference)
};

constexpr AnalyzerCapability operator|(AnalyzerCapability a, AnalyzerCapability b)
{
    return static_cast<AnalyzerCapability>(static_cast<uint32_t>(a) | static_cast<uint32_t>(b));
}
constexpr bool hasCapability(AnalyzerCapability flags, AnalyzerCapability c)
{
    return (static_cast<uint32_t>(flags) & static_cast<uint32_t>(c)) != 0;
}

// Static metadata describing an analyzer's interface.
// Concrete analyzers expose this via a static `info()` method.
struct AnalyzerInfo
{
    std::string id;                          // registry key (e.g. "histogram")
    std::string name;                        // display name
    std::string description;
    std::string version;                     // semantic version string
    AnalyzerCapability capabilities;         // bitwise OR of flags
    std::vector<std::string> outputFields;   // what the UI can display
};
