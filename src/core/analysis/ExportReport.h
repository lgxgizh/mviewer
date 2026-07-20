#pragma once

#include "core/analyzer/Analyzer.h"
#include "core/analyzer/NoiseAnalyzer.h"
#include "core/analyzer/PSNRAnalyzer.h"
#include "core/analyzer/RGBMeanAnalyzer.h"
#include "core/analyzer/SSIMAnalyzer.h"
#include "core/compare/DifferenceEngine.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageFrame.h"

#include <string>
#include <unordered_map>
#include <vector>

namespace mviewer::core
{

// A compare/analysis report between two images. Built by buildCompareReport().
// Pure data + serialization; no Qt, no file I/O (the caller chooses where to
// write the JSON/CSV and the diff PNG).
struct CompareReport
{
    std::string imageA;
    std::string imageB;

    double psnr = 0.0; // dB (PSNR analyzer)
    double ssim = 0.0; // SSIM analyzer

    // Per-image mean RGB.
    double meanR_A = 0, meanG_A = 0, meanB_A = 0;
    double meanR_B = 0, meanG_B = 0, meanB_B = 0;

    // Per-image noise estimate (Laplacian variance).
    double noiseA = 0.0;
    double noiseB = 0.0;

    // Difference summary over DifferenceEngine::differenceMap(A, B).
    double diffMean = 0.0;
    double diffMin = 0.0;
    double diffMax = 0.0;

    // JSON (pretty-ish single line per field) and CSV (header + one row).
    std::string toJson() const;
    std::string toCsv() const;
};

// Run PSNR / SSIM / Noise analyzers + the difference engine over two frames
// and populate a CompareReport. Returns an empty-report-with-paths on invalid
// input (null/decode-failed frames).
CompareReport buildCompareReport(const ImageFrame &a, const ImageFrame &b);

// Heatmap diff image (RGB24) suitable for writing as compare_diff.png via
// Encoder. Returns a null ImageData on invalid input.
ImageData compareDiffImage(const ImageFrame &a, const ImageFrame &b);

// ─── M13.4 batch analyzer export ──────────────────────────────────────────
// Tabular report of running ONE analyzer over MANY images. `columns` is the
// stable union of every metric key seen across `results` (in first-seen
// order); `filenames` and `rows` run in parallel (one entry per result).
// Pure data + serialization; no Qt, no file I/O — the caller writes the file.
struct AnalysisBatchReport
{
    std::string analyzerId;
    std::vector<std::string> columns; // metric keys (excludes "filename")
    std::vector<std::string> filenames;
    std::vector<std::unordered_map<std::string, double>> rows;

    // CSV: header `filename,col1,col2,...` then one row per result. Missing
    // metrics render as an empty field.
    std::string toCsv() const;
    // JSON: {"analyzer":id,"columns":[...],"rows":[{"filename":..,col:val,..}]}
    std::string toJson() const;
};

// Assemble an AnalysisBatchReport from the raw per-image AnalyzerResults
// produced by AnalyzerRegistry::runBatch().
AnalysisBatchReport
buildBatchReport(const std::string &analyzerId,
                 const std::vector<mviewer::analyzer::AnalyzerResult> &results);

} // namespace mviewer::core
