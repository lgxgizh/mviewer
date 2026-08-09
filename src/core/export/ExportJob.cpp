#include "core/export/ExportJob.h"

#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageTransform.h"
#include "domain/Selection.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <cstdint>
#include <filesystem>
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

namespace fs = std::filesystem;

namespace mviewer::exportjob
{

std::string modeName(Mode m)
{
    switch (m)
    {
    case Mode::Convert:
        return "convert";
    case Mode::ContactSheet:
        return "contact";
    case Mode::Pdf:
        return "pdf";
    case Mode::Csv:
        return "csv";
    case Mode::Json:
        return "json";
    case Mode::HtmlReport:
        return "html";
    case Mode::Clipboard:
        return "clipboard";
    }
    return "convert";
}

Mode modeFromName(const std::string &name)
{
    if (name == "contact")
        return Mode::ContactSheet;
    if (name == "pdf")
        return Mode::Pdf;
    if (name == "csv")
        return Mode::Csv;
    if (name == "json")
        return Mode::Json;
    if (name == "html")
        return Mode::HtmlReport;
    if (name == "clipboard")
        return Mode::Clipboard;
    return Mode::Convert;
}

static std::string applyRename(const std::string &pattern, const std::string &base,
                               const std::string &ext, int index, int total)
{
    // Minimal {name} / {seq} / {seq:N} substitution (parity with ImageTransform helper).
    return mviewer::core::applyRenamePattern(pattern, base, ext, index, total);
}

static ImageData maybeResize(const ImageData &d, const ExportJobConfig &cfg)
{
    if (cfg.resizeMode == ResizeMode::Fit)
        return mviewer::core::resizeToFit(d, cfg.resizeValue, cfg.resizeValue);
    if (cfg.resizeMode == ResizeMode::Scale)
        return mviewer::core::resizeByFactor(d, cfg.resizeValue / 100.0);
    return d;
}

static ImageData maybeWatermark(const ImageData &d, const ExportJobConfig &cfg)
{
    if (cfg.watermarkText.empty())
        return d;
    const auto pos = static_cast<mviewer::core::WatermarkPosition>(cfg.watermarkPos);
    return mviewer::core::addTextWatermark(d, cfg.watermarkText, pos, cfg.watermarkOpacity / 100.0,
                                           32);
}

static std::string extensionFor(const std::string &format)
{
    if (format == "jpeg")
        return ".jpg";
    return "." + format;
}

static fs::path pathFromUtf8(const std::string &value)
{
#ifdef _WIN32
    if (value.empty())
        return {};
    if (value.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("UTF-8 path is too long");

    const int inputLength = static_cast<int>(value.size());
    const int length = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(),
                                           inputLength, nullptr, 0);
    if (length == 0)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "invalid UTF-8 path");
    std::wstring wide(static_cast<size_t>(length), L'\0');
    if (MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, value.data(), inputLength,
                            wide.data(), length) == 0)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "invalid UTF-8 path");
    }
    return fs::path(wide);
#else
    return fs::path(value);
#endif
}

static std::string pathToUtf8(const fs::path &path)
{
#ifdef _WIN32
    const std::wstring &wide = path.native();
    if (wide.empty())
        return {};
    if (wide.size() > static_cast<size_t>((std::numeric_limits<int>::max)()))
        throw std::length_error("native path is too long");

    const int inputLength = static_cast<int>(wide.size());
    const int length = WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), inputLength,
                                           nullptr, 0, nullptr, nullptr);
    if (length == 0)
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "cannot encode path as UTF-8");
    std::string utf8(static_cast<size_t>(length), '\0');
    if (WideCharToMultiByte(CP_UTF8, WC_ERR_INVALID_CHARS, wide.data(), inputLength, utf8.data(),
                            length, nullptr, nullptr) == 0)
    {
        throw std::system_error(static_cast<int>(GetLastError()), std::system_category(),
                                "cannot encode path as UTF-8");
    }
    return utf8;
#else
    return path.string();
#endif
}

static std::string outputBaseName(const ExportJobConfig &cfg, const fs::path &source, int index,
                                  int total)
{
    const std::string baseName = pathToUtf8(source.stem());
    std::string sourceExtension = pathToUtf8(source.extension());
    if (!sourceExtension.empty() && sourceExtension.front() == '.')
        sourceExtension.erase(sourceExtension.begin());

    std::string output =
        applyRename(cfg.renamePattern, baseName, sourceExtension, index, total);
    if (output.empty())
        output = baseName;

    static const char *const kImageExtensions[] = {
        ".jpg", ".jpeg", ".png", ".webp", ".tif", ".tiff",
        ".bmp", ".gif",  ".jp2", ".jxl",  ".heic", ".avif"};
    for (const char *extension : kImageExtensions)
    {
        const size_t length = std::char_traits<char>::length(extension);
        if (output.size() <= length)
            continue;

        const std::string tail = output.substr(output.size() - length);
        bool matches = true;
        for (size_t i = 0; i < length; ++i)
        {
            const char a =
                static_cast<char>(std::tolower(static_cast<unsigned char>(tail[i])));
            const char b =
                static_cast<char>(std::tolower(static_cast<unsigned char>(extension[i])));
            if (a != b)
            {
                matches = false;
                break;
            }
        }
        if (matches)
        {
            output.resize(output.size() - length);
            break;
        }
    }
    return output;
}

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

static fs::path uniqueTempPath(const fs::path &destination)
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
    fs::path name = pathFromUtf8(prefix);
    name += destination.stem().native();
    name += destination.extension().native();
    return destination.parent_path() / name;
}

static bool commitTempFile(const fs::path &temporary, const fs::path &destination,
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

ExportJobResult run(const ExportJobConfig &cfg, ProgressFn progress)
{
    ExportJobResult r;
    r.total = static_cast<int>(cfg.sources.size());

    if (cfg.sources.empty())
    {
        r.message = "no sources";
        return r;
    }
    if (cfg.outDir.empty())
    {
        r.message = "no output directory";
        return r;
    }

    // Convert is the unified path. Other modes are still handled by ExportDialog
    // specialized methods; report them as delegated so callers can fall back.
    if (cfg.mode != Mode::Convert)
    {
        r.message = "delegated:" + modeName(cfg.mode);
        return r;
    }

    const std::string ext = extensionFor(cfg.format);
    // M24 (D#2): validate the requested format up front — an unknown format id
    // must fail loudly instead of silently encoding a different format.
    static const std::unordered_set<std::string> kKnownFormats = {
        "jpeg", "jpg", "png", "webp", "tiff", "tif", "bmp"};
    if (!kKnownFormats.count(cfg.format))
    {
        r.failed = r.total;
        r.message = "unsupported format: " + cfg.format;
        return r;
    }

    fs::path outputDirectory;
    std::vector<fs::path> sourcePaths;
    std::vector<fs::path> destinations;
    try
    {
        outputDirectory = pathFromUtf8(cfg.outDir);
        sourcePaths.reserve(cfg.sources.size());
        destinations.reserve(cfg.sources.size());
        for (int i = 0; i < r.total; ++i)
        {
            sourcePaths.push_back(pathFromUtf8(cfg.sources[static_cast<size_t>(i)]));
            const std::string baseName =
                outputBaseName(cfg, sourcePaths.back(), i, r.total);
            destinations.push_back(outputDirectory / pathFromUtf8(baseName + ext));
        }
    }
    catch (const std::exception &error)
    {
        r.failed = r.total;
        r.message = "invalid path encoding: " + std::string(error.what());
        return r;
    }

    PathKeySet sourceKeys;
#ifndef _WIN32
    sourceKeys.reserve(cfg.sources.size());
#endif
    for (const fs::path &source : sourcePaths)
    {
        sourceKeys.insert(normalizedPathKey(source));
    }

    PathKeySet destinationKeys;
    std::unordered_map<FileIdentity, fs::path, FileIdentityHash> destinationFiles;
#ifndef _WIN32
    destinationKeys.reserve(destinations.size());
#endif
    destinationFiles.reserve(destinations.size());
    for (const fs::path &destination : destinations)
    {
        const PathKey key = normalizedPathKey(destination);
        if (sourceKeys.count(key) != 0)
        {
            r.failed = r.total;
            r.message = "source/destination conflict";
            return r;
        }
        if (!destinationKeys.insert(key).second)
        {
            r.failed = r.total;
            r.message = "duplicate destination conflict";
            return r;
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
                return r;
            }
        }
        destinationFiles.try_emplace(*identity, destination);
    }

    if (!destinationFiles.empty())
    {
        for (const fs::path &source : sourcePaths)
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
                return r;
            }
        }
    }

    std::error_code ec;
    fs::create_directories(outputDirectory, ec);
    if (ec)
    {
        r.failed = r.total;
        r.message = "cannot create output directory: " + ec.message();
        return r;
    }

    Encoder::Params params;
    params.quality = cfg.quality;

    for (int i = 0; i < r.total; ++i)
    {
        const std::string &src = cfg.sources[static_cast<size_t>(i)];
        // M24 (D#3): cooperative cancellation between items.
        if (cfg.cancel && cfg.cancel->load(std::memory_order_relaxed))
        {
            r.message = "cancelled after " + std::to_string(r.done) + " / " +
                        std::to_string(r.total);
            return r;
        }
        if (progress)
            progress(i, r.total, src);

        ImageData data = Decoder::decodeFull(src);
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

        const fs::path &dst = destinations[static_cast<size_t>(i)];
        // M24 (D#8): encode to a temp file and atomically rename into place, so
        // an interrupted/failed encode never leaves a partial file at the final
        // name that looks like a successful export. The temp name keeps the
        // REAL extension (".mviewer-tmp-<dst>") because Encoder::encode derives
        // the format from the file suffix.
        const fs::path tmp = uniqueTempPath(dst);
        if (Encoder::encode(data, pathToUtf8(tmp), params))
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
                    r.primaryOutput = pathToUtf8(dst);
            }
        }
        else
        {
            fs::remove(tmp, ec); // best-effort cleanup of the partial temp file
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

} // namespace mviewer::exportjob
