#include "core/batch/BatchProcessor.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/export/ExporterRegistry.h"
#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageTransform.h"
#include "core/image/RawMetadata.h"

#include <algorithm>
#include <atomic>
#include <filesystem>
#include <sstream>

namespace mviewer::core
{
namespace
{

// Extract the file extension (without dot) from a path, lowercased.
std::string extOf(const std::string &path)
{
    const auto pos = path.find_last_of('.');
    if (pos == std::string::npos)
        return {};
    std::string ext = path.substr(pos + 1);
    std::transform(ext.begin(), ext.end(), ext.begin(),
                   [](unsigned char c) { return std::tolower(c); });
    return ext;
}

// Extract the base name (without extension) from a path.
std::string baseNameOf(const std::string &path)
{
    const auto sep = path.find_last_of("/\\");
    std::string fname = (sep != std::string::npos) ? path.substr(sep + 1) : path;
    const auto dot = fname.find_last_of('.');
    if (dot != std::string::npos)
        fname = fname.substr(0, dot);
    return fname;
}

// Map the domain watermark position index to the core enum.
WatermarkPosition mapWatermarkPos(int pos)
{
    switch (pos)
    {
    case 0: return WatermarkPosition::TopLeft;
    case 1: return WatermarkPosition::TopRight;
    case 2: return WatermarkPosition::BottomLeft;
    case 3: return WatermarkPosition::BottomRight;
    case 5: return WatermarkPosition::Tile;
    default: return WatermarkPosition::Center;
    }
}

// Build the output path for an exported file.
std::string buildOutputPath(const domain::BatchJobConfig &config,
                            const std::string &inputPath,
                            int index, int total)
{
    const std::string base = baseNameOf(inputPath);
    const std::string ext = config.exportFormat.empty() ? extOf(inputPath)
                                                         : config.exportFormat;

    std::string outName;
    if (!config.renamePattern.empty())
    {
        outName = applyRenamePattern(config.renamePattern, base, ext,
                                      index, total);
        if (!ext.empty())
            outName += "." + ext;
    }
    else
    {
        outName = base + "." + ext;
    }

    if (config.outputDir.empty())
        return outName;

    std::filesystem::path dir(config.outputDir);
    dir /= outName;
    return dir.string();
}

} // anonymous namespace

domain::BatchFileResult BatchProcessor::processFile(
    const domain::BatchJobConfig &config,
    const std::string &inputPath,
    int fileIndex, int totalFiles)
{
    domain::BatchFileResult result;
    result.inputPath = inputPath;

    // ── Decode ──────────────────────────────────────────────────────
    ImageData img = Decoder::decodeFull(inputPath);
    if (img.isNull())
    {
        result.errorMessage = "Failed to decode image";
        return result;
    }

    result.width = img.width;
    result.height = img.height;

    const int total = totalFiles;

    // ── Apply operations in order ───────────────────────────────────
    for (domain::BatchOp op : config.operations)
    {
        if (m_cancelled.load())
            break;

        switch (op)
        {
        case domain::BatchOp::Analyze:
        {
            // Run each configured analyzer and collect metrics.
            auto frame = ImageFrame::create(inputPath, img);
            for (const auto &analyzerId : config.analyzerIds)
            {
                auto analyzer = AnalyzerRegistry::instance().create(analyzerId);
                if (!analyzer)
                    continue;
                analyzer->analyze(frame);
                auto metrics = analyzer->resultMetrics();
                for (const auto &[k, v] : metrics)
                    result.metrics[analyzerId + "." + k] = v;
            }
            break;
        }

        case domain::BatchOp::Resize:
        {
            img = resizeToFit(img, config.resizeMaxEdge,
                               config.resizeMaxEdge);
            result.width = img.width;
            result.height = img.height;
            break;
        }

        case domain::BatchOp::Watermark:
        {
            if (!config.watermarkText.empty())
            {
                img = addTextWatermark(
                    img, config.watermarkText,
                    mapWatermarkPos(config.watermarkPosition),
                    config.watermarkOpacity, config.watermarkFontSize);
            }
            break;
        }

        case domain::BatchOp::Rename:
            // Rename is handled at export time (pattern applied to output path).
            // No pixel operation needed here.
            break;

        case domain::BatchOp::Export:
        {
            if (config.exportFormat.empty())
                break; // no export requested

            // Determine output path (0-based index; applyRenamePattern adds +1).
            const std::string outPath = buildOutputPath(config, inputPath, fileIndex, total);

            // Ensure output directory exists.
            if (!config.outputDir.empty())
                std::filesystem::create_directories(config.outputDir);

            // Try the exporter registry first, then fall back to Qt encoder.
            bool exported = false;
            auto exporter = ExporterRegistry::instance().get(config.exportFormat + "-exporter");
            if (exporter)
            {
                exported = exporter->exportImage(img, outPath);
            }
            else
            {
                // Fall back: use Qt's QImageWriter via Encoder.
                exported = Encoder::encode(img, outPath, Encoder::Params(config.exportQuality));
            }

            if (exported)
                result.outputPath = outPath;
            else
            {
                result.errorMessage = "Export failed: " + outPath;
                return result;
            }
            break;
        }
        }
    }

    result.success = true;
    return result;
}

domain::BatchJobResult BatchProcessor::execute(const domain::BatchJobConfig &config)
{
    m_cancelled.store(false);
    domain::BatchJobResult aggregate;
    const int total = static_cast<int>(config.inputPaths.size());
    aggregate.fileResults.reserve(total);

    for (int i = 0; i < total; ++i)
    {
        if (m_cancelled.load())
            break;

        if (m_progressCb)
            m_progressCb(i, total, config.inputPaths[static_cast<size_t>(i)]);

        auto fileResult = processFile(config, config.inputPaths[static_cast<size_t>(i)],
                                       i, total);
        if (fileResult.success)
            ++aggregate.totalSucceeded;
        else
            ++aggregate.totalFailed;
        aggregate.fileResults.push_back(std::move(fileResult));
    }

    // Final progress notification.
    if (m_progressCb && !m_cancelled.load())
        m_progressCb(total, total, {});

    return aggregate;
}

} // namespace mviewer::core
