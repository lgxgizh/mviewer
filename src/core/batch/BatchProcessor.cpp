#include "core/batch/BatchProcessor.h"
#include "core/analyzer/Analyzer.h"
#include "core/analyzer/AnalyzerPipeline.h"
#include "core/batch/BatchRename.h"
#include "core/export/ExportJobInternal.h"
#include "core/export/ExporterRegistry.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageFormats.h"
#include "core/image/ImageFrame.h"
#include "core/image/ImageTransform.h"
#include "core/image/RawMetadata.h"

#include <QFile>
#include <QImageReader>
#include <QSize>
#include <QString>

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <filesystem>
#include <sstream>
#include <thread>

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
        fname.erase(dot);
    return fname;
}

// Map the domain watermark position index to the core enum.
WatermarkPosition mapWatermarkPos(int pos)
{
    switch (pos)
    {
    case 0:
        return WatermarkPosition::TopLeft;
    case 1:
        return WatermarkPosition::TopRight;
    case 2:
        return WatermarkPosition::BottomLeft;
    case 3:
        return WatermarkPosition::BottomRight;
    case 5:
        return WatermarkPosition::Tile;
    default:
        return WatermarkPosition::Center;
    }
}

std::string trimmedUtf8(const std::string &text)
{
    const QString value = QString::fromUtf8(text.data(), static_cast<int>(text.size()));
    return value.trimmed().toStdString();
}

// Build the output path for an exported file.
std::string buildOutputPath(const domain::BatchJobConfig &config, const std::string &inputPath,
                            int index, int total)
{
    const std::string ext = config.exportFormat.empty() ? extOf(inputPath) : config.exportFormat;

    // Find/replace on the stem, then the rename pattern, then ".ext" — the
    // order the batch dialog preview shows (BatchRename.h).
    BatchRenameOptions rename;
    rename.pattern = config.renamePattern;
    rename.find = config.renameFind;
    rename.replace = config.renameReplace;
    rename.useRegex = config.renameUseRegex;
    rename.caseSensitive = config.renameCaseSensitive;
    std::string outName = composeRenamedFileName(baseNameOf(inputPath), ext, rename, index, total);
    if (outName.empty() || outName == "." + ext) // replace emptied the stem
        outName = baseNameOf(inputPath) + outName;

    const std::filesystem::path fileName = pathFromUtf8(outName);
    const std::string dir = trimmedUtf8(config.outputDir);
    if (dir.empty())
        return pathToUtf8(pathFromUtf8(inputPath).parent_path() / fileName);
    return pathToUtf8(pathFromUtf8(dir) / fileName);
}

QString normalizedPath(const std::filesystem::path &path)
{
    const std::string utf8 = pathToUtf8(path.lexically_normal());
    QString text = QString::fromUtf8(utf8.data(), static_cast<int>(utf8.size()));
    text.replace(QLatin1Char('\\'), QLatin1Char('/'));
    return text;
}

// equivalent() only works once both paths exist. A not-yet-created destination
// falls back to a case-insensitive compare, which is what Windows will do.
bool sameAsSource(const std::filesystem::path &input, const std::filesystem::path &destination)
{
    std::error_code equivalentError;
    const bool equivalent = std::filesystem::equivalent(input, destination, equivalentError);
    if (!equivalentError)
        return equivalent;

    std::error_code existsError;
    if (std::filesystem::exists(destination, existsError) && !existsError)
        return false;
    const QString left = normalizedPath(input);
    const QString right = normalizedPath(destination);
    return left.compare(right, Qt::CaseInsensitive) == 0;
}

std::string ensureOutputDir(const domain::BatchJobConfig &config)
{
    const std::string dir = trimmedUtf8(config.outputDir);
    if (dir.empty())
        return {};
    std::error_code dirEc;
    std::filesystem::create_directories(pathFromUtf8(dir), dirEc);
    if (!dirEc)
        return {};
    std::string message = "无法创建输出目录：";
    message += dirEc.message();
    return message;
}

std::string checkDestination(const domain::BatchJobConfig &config,
                             const std::filesystem::path &input,
                             const std::filesystem::path &destination)
{
    if (sameAsSource(input, destination))
        return "目标与源文件相同，已跳过";

    std::error_code existsError;
    const bool exists = std::filesystem::exists(destination, existsError);
    if (existsError || !exists || config.overwriteExisting)
        return {};
    std::string message = "目标文件已存在：";
    message += pathToUtf8(destination.filename());
    return message;
}

void removeQuietly(const std::filesystem::path &path)
{
    std::error_code ec;
    std::filesystem::remove(path, ec);
}

std::string exportFailed(const std::filesystem::path &destination)
{
    std::string message = "导出失败：";
    message += pathToUtf8(destination);
    return message;
}

bool encodeToPath(const domain::BatchJobConfig &config, const ImageData &img,
                  const std::string &path)
{
    const std::string exporterId = config.exportFormat + "-exporter";
    const auto exporter = ExporterRegistry::instance().get(exporterId);
    if (exporter)
        return exporter->exportImage(img, path);
    return Encoder::encode(img, path, Encoder::Params(config.exportQuality));
}

bool writeAtomically(const domain::BatchJobConfig &config, const ImageData &img,
                     const std::filesystem::path &destination, std::string &error)
{
    const std::filesystem::path temporary = mviewer::exportjob::uniqueTempPath(destination);
    if (!encodeToPath(config, img, pathToUtf8(temporary)))
    {
        removeQuietly(temporary);
        error = exportFailed(destination);
        return false;
    }

    std::error_code commitError;
    if (!mviewer::exportjob::commitTempFile(temporary, destination, commitError))
    {
        removeQuietly(temporary);
        error = exportFailed(destination);
        return false;
    }
    error.clear();
    return true;
}

std::filesystem::path resolveDestination(const domain::BatchJobConfig &config,
                                         const std::string &inputPath, int index, int total)
{
    return pathFromUtf8(buildOutputPath(config, inputPath, index, total));
}

void applyAnalyzeOp(const domain::BatchJobConfig &config, const std::string &inputPath,
                    const ImageData &img, domain::BatchFileResult &result)
{
    auto frame = ImageFrame::create(inputPath, img);
    for (const auto &analyzerId : config.analyzerIds)
    {
        auto analyzer = AnalyzerRegistry::instance().create(analyzerId);
        if (!analyzer)
            continue;
        analyzer->analyze(frame);
        auto metrics = analyzer->resultMetrics();
        for (const auto &[k, v] : metrics)
        {
            std::string metricKey = analyzerId;
            metricKey += '.';
            metricKey += k;
            result.metrics[metricKey] = v;
        }
    }
}

void applyCropOp(const domain::BatchJobConfig &config, ImageData &img,
                 domain::BatchFileResult &result)
{
    if (config.cropW > 0 && config.cropH > 0)
    {
        img = cropRegion(img, mviewer::domain::Selection{config.cropX, config.cropY, config.cropW,
                                                         config.cropH});
        result.width = img.width;
        result.height = img.height;
    }
}

void applyResizeOp(const domain::BatchJobConfig &config, ImageData &img,
                   domain::BatchFileResult &result)
{
    img = resizeToFit(img, config.resizeMaxEdge, config.resizeMaxEdge);
    result.width = img.width;
    result.height = img.height;
}

void applyWatermarkOp(const domain::BatchJobConfig &config, ImageData &img)
{
    if (!config.watermarkText.empty())
    {
        img = addTextWatermark(img, config.watermarkText, mapWatermarkPos(config.watermarkPosition),
                               config.watermarkOpacity, config.watermarkFontSize);
    }
}

bool applyExportOp(const domain::BatchJobConfig &config, const std::string &inputPath,
                   const ImageData &img, int fileIndex, int totalFiles,
                   domain::BatchFileResult &result)
{
    if (config.exportFormat.empty())
        return true;

    const std::filesystem::path destination =
        resolveDestination(config, inputPath, fileIndex, totalFiles);
    std::string error = ensureOutputDir(config);
    if (error.empty())
        error = checkDestination(config, pathFromUtf8(inputPath), destination);
    const bool ready = error.empty();
    const bool wrote = ready && writeAtomically(config, img, destination, error);
    if (!ready || !wrote)
    {
        result.errorMessage = std::move(error);
        return false;
    }
    result.outputPath = pathToUtf8(destination);
    return true;
}

std::string tooLargeMessage(int width, int height)
{
    std::string message = "图片过大（";
    message += std::to_string(width);
    message += "x";
    message += std::to_string(height);
    message += "），批处理暂不支持";
    return message;
}

// Empty when the claimed raster is inside the batch limit.
std::string oversizedMessage(int width, int height)
{
    constexpr int kMaxSide = 16384;
    constexpr std::int64_t kBytesPerPixel = 4;
    constexpr std::int64_t kMaxBytes = 256LL * 1024LL * 1024LL;
    if (width <= 0 || height <= 0)
        return {};
    if (width > kMaxSide || height > kMaxSide)
        return tooLargeMessage(width, height);
    const std::int64_t pixels = static_cast<std::int64_t>(width) * height;
    if (pixels > kMaxBytes / kBytesPerPixel)
        return tooLargeMessage(width, height);
    return {};
}

std::uint32_t readBe32(const unsigned char *bytes)
{
    return (static_cast<std::uint32_t>(bytes[0]) << 24) |
           (static_cast<std::uint32_t>(bytes[1]) << 16) |
           (static_cast<std::uint32_t>(bytes[2]) << 8) | static_cast<std::uint32_t>(bytes[3]);
}

// QImageReader::size() asks libpng to read through the first IDAT. An IHDR-only
// file ends before that, so the reader reports no size and libpng prints
// "Read Error". The batch guard only needs the claimed dimensions.
bool readPngIhdr(const std::string &path, int &width, int &height)
{
    QFile file(QString::fromUtf8(path.data(), static_cast<int>(path.size())));
    if (!file.open(QIODevice::ReadOnly))
        return false;
    const QByteArray header = file.read(24);
    if (header.size() < 24)
        return false;
    const auto *bytes = reinterpret_cast<const unsigned char *>(header.constData());
    static const unsigned char kSignature[8] = {0x89, 'P', 'N', 'G', '\r', '\n', 0x1a, '\n'};
    if (std::memcmp(bytes, kSignature, 8) != 0 || readBe32(bytes + 8) != 13 ||
        std::memcmp(bytes + 12, "IHDR", 4) != 0)
        return false;
    const std::uint32_t claimedWidth = readBe32(bytes + 16);
    const std::uint32_t claimedHeight = readBe32(bytes + 20);
    constexpr std::uint32_t kMaxInt = 0x7fffffffu;
    if (claimedWidth == 0 || claimedHeight == 0 || claimedWidth > kMaxInt ||
        claimedHeight > kMaxInt)
        return false;
    width = static_cast<int>(claimedWidth);
    height = static_cast<int>(claimedHeight);
    return true;
}

std::string pngSizeGuard(const std::string &inputPath)
{
    int width = 0;
    int height = 0;
    if (!readPngIhdr(inputPath, width, height))
        return {};
    return oversizedMessage(width, height);
}

std::string decodeFailureMessage(const std::string &inputPath)
{
    const std::string fromPng = pngSizeGuard(inputPath);
    if (!fromPng.empty())
        return fromPng;

    const QString path = QString::fromUtf8(inputPath.data(), static_cast<int>(inputPath.size()));
    const QImageReader reader(path);
    const QSize size = reader.size();
    if (!size.isValid() || size.width() <= 0 || size.height() <= 0)
        return "无法解码图片";
    const std::string tooBig = oversizedMessage(size.width(), size.height());
    if (!tooBig.empty())
        return tooBig;
    return "无法解码图片";
}

bool isImageFile(const std::filesystem::path &path)
{
    return ImageFormats::isSupportedPath(pathToUtf8(path));
}

void collectImages(const std::filesystem::path &dir, bool recursive, std::vector<std::string> &out)
{
    std::error_code ec;
    const auto options = std::filesystem::directory_options::skip_permission_denied;
    auto takeFile = [&](const std::filesystem::directory_entry &entry)
    {
        std::error_code fileEc;
        if (entry.is_regular_file(fileEc) && !fileEc && isImageFile(entry.path()))
            out.push_back(pathToUtf8(entry.path()));
    };
    if (recursive)
    {
        for (std::filesystem::recursive_directory_iterator it(dir, options, ec), end;
             !ec && it != end; it.increment(ec))
            takeFile(*it);
        return;
    }
    for (std::filesystem::directory_iterator it(dir, options, ec), end; !ec && it != end;
         it.increment(ec))
        takeFile(*it);
}

std::vector<std::string> expandInputPaths(const domain::BatchJobConfig &config)
{
    std::vector<std::string> expanded;
    expanded.reserve(config.inputPaths.size());
    for (const auto &p : config.inputPaths)
    {
        const std::filesystem::path fsp = pathFromUtf8(p);
        std::error_code typeEc;
        if (std::filesystem::is_directory(fsp, typeEc) && !typeEc)
            collectImages(fsp, config.recursiveScan, expanded);
        else
            expanded.push_back(p);
    }
    return expanded;
}

} // anonymous namespace

domain::BatchFileResult BatchProcessor::processFile(const domain::BatchJobConfig &config,
                                                    const std::string &inputPath, int fileIndex,
                                                    int totalFiles)
{
    domain::BatchFileResult result;
    result.inputPath = inputPath;

    if (m_cancelled.load())
    {
        result.errorMessage = "已取消";
        return result;
    }

    // Reject an oversized claim before decode. A full read of a truncated PNG
    // only produces "libpng error: Read Error" and hides the IHDR dimensions.
    const std::string tooBig = pngSizeGuard(inputPath);
    if (!tooBig.empty())
    {
        result.errorMessage = tooBig;
        return result;
    }

    // ── Decode ──────────────────────────────────────────────────────
    ImageData img = Decoder::decodeFull(inputPath);
    if (m_cancelled.load())
    {
        result.errorMessage = "已取消";
        return result;
    }
    if (img.isNull())
    {
        result.errorMessage = decodeFailureMessage(inputPath);
        return result;
    }

    result.width = img.width;
    result.height = img.height;

    // ── Apply operations in order ───────────────────────────────────
    for (domain::BatchOp op : config.operations)
    {
        if (m_cancelled.load())
        {
            result.errorMessage = "已取消";
            return result;
        }

        switch (op)
        {
        case domain::BatchOp::Analyze:
            applyAnalyzeOp(config, inputPath, img, result);
            break;

        case domain::BatchOp::Crop:
            applyCropOp(config, img, result);
            break;

        case domain::BatchOp::Resize:
            applyResizeOp(config, img, result);
            break;

        case domain::BatchOp::Watermark:
            applyWatermarkOp(config, img);
            break;

        case domain::BatchOp::Rename:
            // Rename is handled at export time (pattern applied to output path).
            break;

        case domain::BatchOp::Export:
            if (!applyExportOp(config, inputPath, img, fileIndex, totalFiles, result))
                return result;
            break;
        }
    }

    result.success = true;
    return result;
}

domain::BatchJobResult BatchProcessor::execute(const domain::BatchJobConfig &config)
{
    domain::BatchJobResult aggregate;

    // Expand directory inputs. recursiveScan walks subfolders; otherwise only
    // the directory's own image files are collected so "添加目录" still works.
    const std::vector<std::string> expandedPaths = expandInputPaths(config);
    const int total = static_cast<int>(expandedPaths.size());

    // Do not clear a cancel that won the race against worker startup. A fresh
    // processor starts uncancelled; reuse after a finished cancel stays stopped.
    if (m_cancelled.load())
    {
        aggregate.cancelled = true;
        aggregate.totalSkipped = total;
        return aggregate;
    }
    m_paused.store(false);
    aggregate.fileResults.reserve(static_cast<size_t>(total));

    // P2 #⑦: retry counter (config.retryCount, default 0 = no retry).
    for (int i = 0; i < total; ++i)
    {
        // Honour pause between files (current file always finishes first).
        waitWhilePaused();
        if (m_cancelled.load())
            break;

        if (m_progressCb)
            m_progressCb(i, total, expandedPaths[static_cast<size_t>(i)]);
        // The progress callback is the UI's chance to cancel before this file
        // starts. Honour it here so the file is skipped, not reported as done.
        if (m_cancelled.load())
            break;

        domain::BatchFileResult fileResult;
        bool succeeded = false;
        for (int attempt = 0; attempt <= config.retryCount && !succeeded; ++attempt)
        {
            fileResult = processFile(config, expandedPaths[static_cast<size_t>(i)], i, total);
            if (fileResult.success)
            {
                succeeded = true;
                break;
            }
            if (m_cancelled.load())
                break;
            if (attempt < config.retryCount && config.retryDelayMs > 0)
            {
                std::this_thread::sleep_for(std::chrono::milliseconds(config.retryDelayMs));
            }
        }
        if (succeeded || fileResult.success)
            ++aggregate.totalSucceeded;
        else
            ++aggregate.totalFailed;
        // Stream the result before it is moved into the aggregate so a UI can
        // report progress per file instead of waiting for the whole job.
        if (m_fileResultCb)
            m_fileResultCb(fileResult);
        aggregate.fileResults.push_back(std::move(fileResult));
    }

    if (m_cancelled.load())
    {
        aggregate.cancelled = true;
        const int finished = static_cast<int>(aggregate.fileResults.size());
        aggregate.totalSkipped = total > finished ? total - finished : 0;
    }

    // Final progress notification.
    if (m_progressCb && !m_cancelled.load())
        m_progressCb(total, total, {});

    return aggregate;
}

} // namespace mviewer::core
