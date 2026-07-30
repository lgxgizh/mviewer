#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

namespace mviewer::domain
{

// Operations a batch job can apply to each image, in execution order.
enum class BatchOp : uint8_t
{
    Analyze = 0, // run selected analyzers, collect metrics
    Crop,        // P2 #⑦: crop to rectangle (x, y, w, h)
    Resize,      // resize to fit max edge (keeps aspect ratio)
    Watermark,   // draw text watermark
    Rename,      // apply rename pattern to output filename
    Export       // export to a different format
};

// Result of processing a single file in a batch.
struct BatchFileResult
{
    std::string inputPath;
    std::string outputPath; // empty if not exported
    bool success = false;
    std::string errorMessage;                        // empty on success
    std::unordered_map<std::string, double> metrics; // analyzer metrics
    int width = 0;
    int height = 0;
};

// Aggregate result of a complete batch run.
struct BatchJobResult
{
    std::vector<BatchFileResult> fileResults;
    int totalSucceeded = 0;
    int totalFailed = 0;
};

// Full configuration for a batch job.
struct BatchJobConfig
{
    // Input files to process.
    std::vector<std::string> inputPaths;

    // Ordered list of operations to apply.
    std::vector<BatchOp> operations;

    // --- Resize params ---
    int resizeMaxEdge = 1920; // target long edge in px

    // --- Watermark params ---
    std::string watermarkText;
    int watermarkPosition = 4; // 0=TL 1=TR 2=BL 3=BR 4=Center 5=Tile
    double watermarkOpacity = 0.3;
    int watermarkFontSize = 24;

    // --- Rename params ---
    std::string renamePattern; // e.g. "{name}_batched_{seq:3}"

    // --- Export params ---
    std::string exportFormat; // "png", "jpg", "bmp" — empty = skip export
    int exportQuality = 90;   // JPEG quality 1-100
    std::string outputDir;    // destination directory for exported files

    // --- Analyze params ---
    std::vector<std::string> analyzerIds; // which analyzers to run

    // ─── P2 #⑦: retry & crop ────────────────────────────────────────────────
    int retryCount = 0;     // number of retries per file on failure (0 = no retry)
    int retryDelayMs = 500; // delay between retries in ms

    // --- Crop params (used when BatchOp::Crop is in operations) ---
    int cropX = 0;
    int cropY = 0;
    int cropW = 0; // 0 = no crop
    int cropH = 0;

    // --- Directory recursion ---
    bool recursiveScan = false; // scan subdirectories for images
};

} // namespace mviewer::domain
