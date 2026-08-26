#include "core/export/ExportJob.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageRepository.h"
#include "core/image/ImageTransform.h"
#include "core/image/QtConvert.h"
#include "domain/Selection.h"

#include <atomic>
#include <algorithm>
#include <cctype>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <iomanip>
#include <limits>
#include <optional>
#include <set>
#include <sstream>
#include <stdexcept>
#include <system_error>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#ifdef _WIN32
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#else
#include <sys/stat.h>
#include <unistd.h>
#endif

#include "core/export/ExportJobInternal.h"

namespace fs = std::filesystem;

namespace mviewer::exportjob
{

#ifdef _WIN32
using PathKey = std::wstring;

struct PathKeyLess
{
    bool operator()(const PathKey &left, const PathKey &right) const noexcept
    {
        const int result = CompareStringOrdinal(left.data(), static_cast<int>(left.size()),
                                                right.data(), static_cast<int>(right.size()), TRUE);
        return result == CSTR_LESS_THAN;
    }
};

using PathKeySet = std::set<PathKey, PathKeyLess>;
#else
using PathKey = std::string;
using PathKeySet = std::unordered_set<PathKey>;
#endif

static PathKey normalizedPathKey(const fs::path &path)
{
    std::error_code ec;
    fs::path normalized = fs::absolute(path, ec);
    if (ec)
        normalized = path;
    normalized = normalized.lexically_normal();
#ifdef _WIN32
    return normalized.native();
#else
    return normalized.native();
#endif
}

struct FileIdentity
{
    std::uint64_t device = 0;
    std::uint64_t file = 0;

    bool operator==(const FileIdentity &) const = default;
};

struct FileIdentityHash
{
    size_t operator()(const FileIdentity &identity) const noexcept
    {
        const size_t first = std::hash<std::uint64_t>{}(identity.device);
        const size_t second = std::hash<std::uint64_t>{}(identity.file);
        return first ^ (second + 0x9e3779b9U + (first << 6U) + (first >> 2U));
    }
};

static std::optional<FileIdentity> fileIdentity(const fs::path &path)
{
#ifdef _WIN32
    const HANDLE handle = CreateFileW(
        path.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr);
    if (handle == INVALID_HANDLE_VALUE)
        return std::nullopt;

    BY_HANDLE_FILE_INFORMATION info{};
    const BOOL succeeded = GetFileInformationByHandle(handle, &info);
    CloseHandle(handle);
    if (!succeeded)
        return std::nullopt;

    const std::uint64_t file = (static_cast<std::uint64_t>(info.nFileIndexHigh) << 32U) |
                               static_cast<std::uint64_t>(info.nFileIndexLow);
    return FileIdentity{static_cast<std::uint64_t>(info.dwVolumeSerialNumber), file};
#else
    struct stat info{};
    if (::stat(path.c_str(), &info) != 0)
        return std::nullopt;
    return FileIdentity{static_cast<std::uint64_t>(info.st_dev),
                        static_cast<std::uint64_t>(info.st_ino)};
#endif
}

fs::path uniqueTempPath(const fs::path &destination)
{
    static std::atomic<unsigned long long> sequence{0};
    const auto ticks = std::chrono::steady_clock::now().time_since_epoch().count();
#ifdef _WIN32
    const unsigned long processId = GetCurrentProcessId();
#else
    const unsigned long processId = static_cast<unsigned long>(getpid());
#endif
    const auto item = sequence.fetch_add(1, std::memory_order_relaxed);
    const std::string prefix = ".mviewer-tmp-" + std::to_string(processId) + "-" +
                               std::to_string(ticks) + "-" + std::to_string(item) + "-";
    fs::path name = mviewer::core::pathFromUtf8(prefix);
    name += destination.stem().native();
    name += destination.extension().native();
    return destination.parent_path() / name;
}

bool commitTempFile(const fs::path &temporary, const fs::path &destination,
                           std::error_code &error)
{
#ifdef _WIN32
    const DWORD attributes = GetFileAttributesW(destination.c_str());
    BOOL committed = FALSE;
    if (attributes != INVALID_FILE_ATTRIBUTES)
    {
        committed = ReplaceFileW(destination.c_str(), temporary.c_str(), nullptr, 0, nullptr,
                                 nullptr);
    }
    else
    {
        committed = MoveFileExW(temporary.c_str(), destination.c_str(), MOVEFILE_WRITE_THROUGH);
    }
    if (!committed)
    {
        error = std::error_code(static_cast<int>(GetLastError()), std::system_category());
        return false;
    }
    error.clear();
    return true;
#else
    fs::rename(temporary, destination, error);
    return !error;
#endif
}


static ExportJobResult runClipboardMode(const ExportJobConfig &cfg,
                                        const std::vector<std::string> &sources,
                                        ProgressFn progress, ExportJobResult r)
{
    if (progress)
        progress(0, r.total, sources.front());
    r.clipboardImage = decodeSource(sources.front(), cfg);
    if (r.clipboardImage.isNull())
    {
        r.failed = 1;
        r.message = "clipboard source decode failed";
        return r;
    }
    r.done = 1;
    r.message = "clipboard image ready";
    if (progress)
        progress(1, r.total, {});
    return r;
}

static ExportJobResult runContactOrPdf(const ExportJobConfig &cfg,
                                       const std::vector<std::string> &sources,
                                       const fs::path &outputDirectory, ProgressFn progress,
                                       ExportJobResult r)
{
    if (cfg.mode == Mode::ContactSheet || cfg.mode == Mode::Pdf)
    {
        // Contact/PDF materialize bounded scaled pages, never a vector of
        // full-resolution source frames. The final writer runs only after all
        // worker-side decode work succeeds or has been accounted for.
        std::vector<ImageData> images;
        images.reserve(sources.size());
        std::size_t stagedBytes = 0;
        const int maxEdge = cfg.mode == Mode::ContactSheet
                                ? std::clamp(cfg.contactThumb, 16, 2000)
                                : std::clamp(std::max(cfg.contactThumb, 512), 256, 2048);
        for (int i = 0; i < r.total; ++i)
        {
            if (cfg.cancel && cfg.cancel->load(std::memory_order_relaxed))
            {
                r.message = "cancelled after " + std::to_string(i) + " / " +
                            std::to_string(r.total);
                return r;
            }
            if (progress)
                progress(i, r.total, sources[static_cast<size_t>(i)]);
            ImageData image = Decoder::decodeScaled(sources[static_cast<size_t>(i)], maxEdge);
            if (image.isNull())
            {
                ++r.failed;
                continue;
            }
            if (stagedBytes > cfg.stagingMemoryBudgetBytes ||
                image.byteSize() > cfg.stagingMemoryBudgetBytes - stagedBytes)
            {
                r.failed = r.total;
                r.message = "staging memory budget exceeded";
                return r;
            }
            stagedBytes += image.byteSize();
            images.push_back(std::move(image));
        }
        if (images.empty())
        {
            r.message = "no source image decoded";
            return r;
        }

        const fs::path destination = outputDirectory /
                                     mviewer::core::pathFromUtf8(cfg.mode == Mode::ContactSheet
                                                       ? "contact_sheet.png"
                                                       : "export.pdf");
        const fs::path temporary = uniqueTempPath(destination);
        bool written = false;
        if (cfg.mode == Mode::ContactSheet)
        {
            const ImageData sheet =
                mviewer::core::makeContactSheet(images, std::clamp(cfg.contactCols, 1, 20),
                                                 std::clamp(cfg.contactThumb, 16, 2000));
            written = !sheet.isNull() &&
                      Encoder::encode(sheet, mviewer::core::pathToUtf8(temporary),
                                      Encoder::Params{cfg.quality});
        }
        else
        {
            written = mviewer::core::writePdf(mviewer::core::pathToUtf8(temporary), images,
                                              cfg.quality);
        }
        if (!written)
        {
            std::error_code cleanup;
            fs::remove(temporary, cleanup);
            r.message = "output generation failed";
            return r;
        }
        std::error_code commitError;
        if (!commitTempFile(temporary, destination, commitError))
        {
            std::error_code cleanup;
            fs::remove(temporary, cleanup);
            r.message = "output commit failed: " + commitError.message();
            return r;
        }
        r.done = 1;
        r.primaryOutput = mviewer::core::pathToUtf8(destination);
        if (progress)
            progress(r.total, r.total, {});
        r.message = std::string("done 1 output") + (r.failed ? " (some sources failed)" : "");
        return r;
    }

    return r;
}

static ExportJobResult runReportExport(const ExportJobConfig &cfg,
                                       const std::vector<std::string> &sources,
                                       const fs::path &outputDirectory, ProgressFn progress,
                                       ExportJobResult r)
{
    if (cfg.mode == Mode::Csv || cfg.mode == Mode::Json || cfg.mode == Mode::HtmlReport)
    {
        std::vector<ReportRow> rows;
        rows.reserve(sources.size());
        for (int i = 0; i < r.total; ++i)
        {
            if (cfg.cancel && cfg.cancel->load(std::memory_order_relaxed))
            {
                r.message = "cancelled after " + std::to_string(i) + " / " +
                            std::to_string(r.total);
                return r;
            }
            if (progress)
                progress(i, r.total, sources[static_cast<size_t>(i)]);
            try
            {
                const auto row = analyzeSource(
                    mviewer::core::pathFromUtf8(sources[static_cast<size_t>(i)]));
                if (row)
                    rows.push_back(*row);
                else
                    ++r.failed;
            }
            catch (...)
            {
                ++r.failed;
            }
        }
        if (rows.empty())
        {
            r.message = "no source image decoded";
            return r;
        }

        std::string body;
        std::string extension;
        if (cfg.mode == Mode::Csv)
        {
            extension = ".csv";
            std::ostringstream out;
            out << "Name,Width,Height,LumMean,RMean,GMean,BMean\n";
            for (const auto &row : rows)
                out << csvEscape(row.name) << "," << row.width << "," << row.height << ","
                    << fixedNumber(row.lumMean) << "," << fixedNumber(row.rMean) << ","
                    << fixedNumber(row.gMean) << "," << fixedNumber(row.bMean) << "\n";
            body = out.str();
        }
        else if (cfg.mode == Mode::Json)
        {
            extension = ".json";
            std::ostringstream out;
            out << "{\n  \"images\": [";
            for (size_t i = 0; i < rows.size(); ++i)
            {
                if (i)
                    out << ",";
                const auto &row = rows[i];
                out << "\n    {\"name\":\"" << jsonEscape(row.name) << "\",\"width\":"
                    << row.width << ",\"height\":" << row.height << ",\"lumMean\":"
                    << jsonNumber(row.lumMean) << ",\"rMean\":" << jsonNumber(row.rMean)
                    << ",\"gMean\":" << jsonNumber(row.gMean) << ",\"bMean\":"
                    << jsonNumber(row.bMean) << "}";
            }
            out << "\n  ]\n}\n";
            body = out.str();
        }
        else
        {
            extension = ".html";
            std::ostringstream out;
            out << "<!doctype html><html><head><meta charset=\"utf-8\"><title>Export "
                   "Report</title></head><body><h1>Export Report</h1><table><tr>"
                   "<th>Name</th><th>Width</th><th>Height</th><th>LumMean</th><th>R</th>"
                   "<th>G</th><th>B</th></tr>\n";
            for (const auto &row : rows)
                out << "<tr><td>" << htmlEscape(row.name) << "</td><td>" << row.width
                    << "</td><td>" << row.height << "</td><td>" << fixedNumber(row.lumMean)
                    << "</td><td>" << fixedNumber(row.rMean) << "</td><td>"
                    << fixedNumber(row.gMean) << "</td><td>" << fixedNumber(row.bMean)
                    << "</td></tr>\n";
            out << "</table></body></html>\n";
            body = out.str();
        }
        const fs::path destination =
            outputDirectory / mviewer::core::pathFromUtf8("export_report" + extension);
        if (!writeTextAtomically(destination, body))
        {
            r.message = "report write failed";
            return r;
        }
        r.done = 1;
        r.primaryOutput = mviewer::core::pathToUtf8(destination);
        if (progress)
            progress(r.total, r.total, {});
        r.message = std::string("done 1 report") + (r.failed ? " (some sources failed)" : "");
        return r;
    }

    return r;
}

struct ConvertPlan
{
    std::vector<fs::path> sourcePaths;
    std::vector<fs::path> destinations;
};

static std::optional<ConvertPlan> makeConvertPlan(const ExportJobConfig &cfg,
                                                   const std::vector<std::string> &sources,
                                                   const fs::path &outputDirectory,
                                                   ExportJobResult &r)
{
    ConvertPlan plan;
    try
    {
        plan.sourcePaths.reserve(sources.size());
        plan.destinations.reserve(sources.size());
        if (!cfg.destinationPath.empty() && r.total != 1)
        {
            r.failed = r.total;
            r.message = "explicit destination requires one source";
            return std::nullopt;
        }
        for (int i = 0; i < r.total; ++i)
        {
            plan.sourcePaths.push_back(
                mviewer::core::pathFromUtf8(sources[static_cast<size_t>(i)]));
            if (!cfg.destinationPath.empty())
                plan.destinations.push_back(mviewer::core::pathFromUtf8(cfg.destinationPath));
            else
            {
                const std::string baseName =
                    outputBaseName(cfg, plan.sourcePaths.back(), i, r.total);
                plan.destinations.push_back(
                    outputDirectory /
                    mviewer::core::pathFromUtf8(baseName + extensionFor(cfg.format)));
            }
        }
    }
    catch (const std::exception &error)
    {
        r.failed = r.total;
        r.message = "invalid path encoding: " + std::string(error.what());
        return std::nullopt;
    }

    PathKeySet sourceKeys;
#ifndef _WIN32
    sourceKeys.reserve(sources.size());
#endif
    for (const fs::path &source : plan.sourcePaths)
        sourceKeys.insert(normalizedPathKey(source));

    PathKeySet destinationKeys;
    std::unordered_map<FileIdentity, fs::path, FileIdentityHash> destinationFiles;
#ifndef _WIN32
    destinationKeys.reserve(plan.destinations.size());
#endif
    destinationFiles.reserve(plan.destinations.size());
    for (const fs::path &destination : plan.destinations)
    {
        const PathKey key = normalizedPathKey(destination);
        if (sourceKeys.count(key) != 0)
        {
            r.failed = r.total;
            r.message = "source/destination conflict";
            return std::nullopt;
        }
        if (!destinationKeys.insert(key).second)
        {
            r.failed = r.total;
            r.message = "duplicate destination conflict";
            return std::nullopt;
        }

        const auto identity = fileIdentity(destination);
        if (!identity)
            continue;
        if (const auto previous = destinationFiles.find(*identity);
            previous != destinationFiles.end())
        {
            std::error_code equivalentError;
            if (fs::equivalent(previous->second, destination, equivalentError) && !equivalentError)
            {
                r.failed = r.total;
                r.message = "duplicate destination conflict";
                return std::nullopt;
            }
        }
        destinationFiles.try_emplace(*identity, destination);
    }

    for (const fs::path &source : plan.sourcePaths)
    {
        const auto identity = fileIdentity(source);
        if (!identity)
            continue;
        const auto destination = destinationFiles.find(*identity);
        if (destination == destinationFiles.end())
            continue;
        std::error_code equivalentError;
        if (fs::equivalent(source, destination->second, equivalentError) && !equivalentError)
        {
            r.failed = r.total;
            r.message = "source/destination conflict";
            return std::nullopt;
        }
    }
    return plan;
}

static ExportJobResult runConvert(const ExportJobConfig &cfg,
                                  const std::vector<std::string> &sources,
                                  const fs::path &outputDirectory, ProgressFn progress,
                                  ExportJobResult r)
{
    const auto plan = makeConvertPlan(cfg, sources, outputDirectory, r);
    if (!plan)
        return r;

    Encoder::Params params;
    params.quality = cfg.quality;
    std::error_code ec;
    for (int i = 0; i < r.total; ++i)
    {
        const std::string &src = sources[static_cast<size_t>(i)];
        if (cfg.cancel && cfg.cancel->load(std::memory_order_relaxed))
        {
            r.message = "cancelled after " + std::to_string(r.done) + " / " +
                        std::to_string(r.total);
            return r;
        }
        if (progress)
            progress(i, r.total, src);

        ImageData data = decodeSource(src, cfg);
        if (data.isNull())
        {
            ++r.failed;
            continue;
        }
        data = maybeResize(data, cfg);
        if (cfg.cropEnabled && cfg.cropW > 0 && cfg.cropH > 0)
            data = cropRegion(
                data, mviewer::domain::Selection{cfg.cropX, cfg.cropY, cfg.cropW, cfg.cropH});
        data = maybeWatermark(data, cfg);

        const fs::path &dst = plan->destinations[static_cast<size_t>(i)];
        const fs::path tmp = uniqueTempPath(dst);
        if (Encoder::encode(data, mviewer::core::pathToUtf8(tmp), params))
        {
            std::error_code tec;
            if (!commitTempFile(tmp, dst, tec))
            {
                std::error_code cleanupError;
                fs::remove(tmp, cleanupError);
                ++r.failed;
            }
            else
            {
                ++r.done;
                if (r.primaryOutput.empty())
                    r.primaryOutput = mviewer::core::pathToUtf8(dst);
            }
        }
        else
        {
            fs::remove(tmp, ec);
            ++r.failed;
        }
    }

    if (progress)
        progress(r.total, r.total, {});
    std::ostringstream oss;
    oss << "done " << r.done << " / " << r.total;
    if (r.failed)
        oss << " (failed " << r.failed << ")";
    r.message = oss.str();
    return r;
}

ExportJobResult run(const ExportJobConfig &cfg, ProgressFn progress)
{
    std::vector<std::string> sources = cfg.sources;
    if (sources.empty() && !cfg.sourceDirectory.empty())
    {
        try
        {
            const fs::path sourceDirectory = mviewer::core::pathFromUtf8(cfg.sourceDirectory);
            std::error_code listingError;
            for (fs::directory_iterator it(sourceDirectory, listingError), end;
                 !listingError && it != end; it.increment(listingError))
            {
                std::error_code typeError;
                if (!it->is_regular_file(typeError) || typeError)
                    continue;
                std::string extension = mviewer::core::pathToUtf8(it->path().extension());
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                static const std::unordered_set<std::string> imageExtensions = {
                    ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"};
                if (imageExtensions.count(extension) != 0)
                    sources.push_back(mviewer::core::pathToUtf8(it->path()));
            }
            std::sort(sources.begin(), sources.end());
        }
        catch (const std::exception &error)
        {
            ExportJobResult failed;
            failed.message = "source directory enumeration failed: " + std::string(error.what());
            return failed;
        }
    }

    ExportJobResult r;
    r.total = static_cast<int>(sources.size());
    if (sources.empty())
    {
        r.message = "no sources";
        return r;
    }
    if (cfg.outDir.empty() && cfg.mode != Mode::Clipboard)
    {
        r.message = "no output directory";
        return r;
    }

    fs::path outputDirectory;
    try
    {
        if (!cfg.outDir.empty())
            outputDirectory = mviewer::core::pathFromUtf8(cfg.outDir);
    }
    catch (const std::exception &error)
    {
        r.failed = r.total;
        r.message = "invalid path encoding: " + std::string(error.what());
        return r;
    }

    if (cfg.mode == Mode::Clipboard)
        return runClipboardMode(cfg, sources, progress, r);

    std::error_code outputError;
    fs::create_directories(outputDirectory, outputError);
    if (outputError)
    {
        r.failed = r.total;
        r.message = "cannot create output directory: " + outputError.message();
        return r;
    }

    if (cfg.mode == Mode::ContactSheet || cfg.mode == Mode::Pdf)
        return runContactOrPdf(cfg, sources, outputDirectory, progress, r);
    if (cfg.mode == Mode::Csv || cfg.mode == Mode::Json || cfg.mode == Mode::HtmlReport)
        return runReportExport(cfg, sources, outputDirectory, progress, r);

    static const std::unordered_set<std::string> kKnownFormats = {
        "jpeg", "jpg", "png", "webp", "tiff", "tif", "bmp"};
    if (!kKnownFormats.count(cfg.format))
    {
        r.failed = r.total;
        r.message = "unsupported format: " + cfg.format;
        return r;
    }
    return runConvert(cfg, sources, outputDirectory, progress, r);
}

} // namespace mviewer::exportjob
