#include "core/export/ExportJob.h"

#include "core/analysis/AnalysisEngine.h"
#include "core/image/Decoder.h"
#include "core/image/Encoder.h"
#include "core/image/ImageBuffer.h"
#include "core/image/ImageTransform.h"
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

static fs::path pathFromUtf8(const std::string &value);
static std::string pathToUtf8(const fs::path &path);
static fs::path uniqueTempPath(const fs::path &destination);
static bool commitTempFile(const fs::path &temporary, const fs::path &destination,
                           std::error_code &error);

struct ReportRow
{
    std::string name;
    int width = 0;
    int height = 0;
    double lumMean = 0.0;
    double rMean = 0.0;
    double gMean = 0.0;
    double bMean = 0.0;
};

static std::string csvEscape(const std::string &value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos)
        return value;
    std::string out = "\"";
    for (const char c : value)
        out += c == '"' ? "\"\"" : std::string(1, c);
    out += '"';
    return out;
}

static std::string jsonEscape(const std::string &value)
{
    std::string out;
    out.reserve(value.size() + 2);
    for (const unsigned char c : value)
    {
        switch (c)
        {
        case '"':
            out += "\\\"";
            break;
        case '\\':
            out += "\\\\";
            break;
        case '\n':
            out += "\\n";
            break;
        case '\r':
            out += "\\r";
            break;
        case '\t':
            out += "\\t";
            break;
        default:
            if (c < 0x20)
            {
                const char hex[] = "0123456789abcdef";
                out += "\\u00";
                out += hex[c >> 4];
                out += hex[c & 0x0f];
            }
            else
                out += static_cast<char>(c);
            break;
        }
    }
    return out;
}

static std::string htmlEscape(const std::string &value)
{
    std::string out;
    for (const char c : value)
    {
        switch (c)
        {
        case '&':
            out += "&amp;";
            break;
        case '<':
            out += "&lt;";
            break;
        case '>':
            out += "&gt;";
            break;
        case '"':
            out += "&quot;";
            break;
        case '\'':
            out += "&#39;";
            break;
        default:
            out += c;
            break;
        }
    }
    return out;
}

static std::string fixedNumber(double value)
{
    if (!std::isfinite(value))
        return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

static std::string jsonNumber(double value)
{
    const std::string number = fixedNumber(value);
    return number.empty() ? "null" : number;
}

static std::optional<ReportRow> analyzeSource(const fs::path &path)
{
    ImageData image = Decoder::decodeFull(pathToUtf8(path));
    if (image.isNull())
        return std::nullopt;
    const ImageStats stats = AnalysisEngine::computeStats(image);
    ReportRow row;
    row.name = pathToUtf8(path.filename());
    row.width = image.width;
    row.height = image.height;
    row.lumMean = stats.lumMean;
    row.rMean = stats.rMean;
    row.gMean = stats.gMean;
    row.bMean = stats.bMean;
    return row;
}

static bool writeTextAtomically(const fs::path &destination, const std::string &contents)
{
    const fs::path temporary = uniqueTempPath(destination);
    {
        std::ofstream file(temporary, std::ios::binary);
        if (!file)
            return false;
        file.write(contents.data(), static_cast<std::streamsize>(contents.size()));
        if (!file)
        {
            std::error_code cleanup;
            fs::remove(temporary, cleanup);
            return false;
        }
    }
    std::error_code error;
    if (commitTempFile(temporary, destination, error))
        return true;
    std::error_code cleanup;
    fs::remove(temporary, cleanup);
    return false;
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
    std::vector<std::string> sources = cfg.sources;
    if (sources.empty() && !cfg.sourceDirectory.empty())
    {
        try
        {
            const fs::path sourceDirectory = pathFromUtf8(cfg.sourceDirectory);
            std::error_code listingError;
            for (fs::directory_iterator it(sourceDirectory, listingError), end;
                 !listingError && it != end; it.increment(listingError))
            {
                std::error_code typeError;
                if (!it->is_regular_file(typeError) || typeError)
                    continue;
                std::string extension = pathToUtf8(it->path().extension());
                std::transform(extension.begin(), extension.end(), extension.begin(),
                               [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
                static const std::unordered_set<std::string> imageExtensions = {
                    ".png", ".jpg", ".jpeg", ".bmp", ".webp", ".tif", ".tiff"};
                if (imageExtensions.count(extension) != 0)
                    sources.push_back(pathToUtf8(it->path()));
            }
            std::sort(sources.begin(), sources.end());
        }
        catch (const std::exception &error)
        {
            ExportJobResult failed;
            failed.message = "source directory enumeration failed: " +
                             std::string(error.what());
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
            outputDirectory = pathFromUtf8(cfg.outDir);
    }
    catch (const std::exception &error)
    {
        r.failed = r.total;
        r.message = "invalid path encoding: " + std::string(error.what());
        return r;
    }

    if (cfg.mode == Mode::Clipboard)
    {
        if (progress)
            progress(0, r.total, sources.front());
        r.clipboardImage = Decoder::decodeFull(sources.front());
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

    std::error_code outputError;
    fs::create_directories(outputDirectory, outputError);
    if (outputError)
    {
        r.failed = r.total;
        r.message = "cannot create output directory: " + outputError.message();
        return r;
    }

    if (cfg.mode == Mode::ContactSheet || cfg.mode == Mode::Pdf)
    {
        // Contact/PDF materialize bounded scaled pages, never a vector of
        // full-resolution source frames. The final writer runs only after all
        // worker-side decode work succeeds or has been accounted for.
        std::vector<ImageData> images;
        images.reserve(sources.size());
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
            images.push_back(std::move(image));
        }
        if (images.empty())
        {
            r.message = "no source image decoded";
            return r;
        }

        const fs::path destination = outputDirectory /
                                     pathFromUtf8(cfg.mode == Mode::ContactSheet
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
                      Encoder::encode(sheet, pathToUtf8(temporary), Encoder::Params{cfg.quality});
        }
        else
        {
            written = mviewer::core::writePdf(pathToUtf8(temporary), images, cfg.quality);
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
        r.primaryOutput = pathToUtf8(destination);
        if (progress)
            progress(r.total, r.total, {});
        r.message = std::string("done 1 output") + (r.failed ? " (some sources failed)" : "");
        return r;
    }

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
                const auto row = analyzeSource(pathFromUtf8(sources[static_cast<size_t>(i)]));
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
        const fs::path destination = outputDirectory / pathFromUtf8("export_report" + extension);
        if (!writeTextAtomically(destination, body))
        {
            r.message = "report write failed";
            return r;
        }
        r.done = 1;
        r.primaryOutput = pathToUtf8(destination);
        if (progress)
            progress(r.total, r.total, {});
        r.message = std::string("done 1 report") + (r.failed ? " (some sources failed)" : "");
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

    std::vector<fs::path> sourcePaths;
    std::vector<fs::path> destinations;
    try
    {
        outputDirectory = pathFromUtf8(cfg.outDir);
        sourcePaths.reserve(sources.size());
        destinations.reserve(sources.size());
        for (int i = 0; i < r.total; ++i)
        {
            sourcePaths.push_back(pathFromUtf8(sources[static_cast<size_t>(i)]));
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
    sourceKeys.reserve(sources.size());
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
        const std::string &src = sources[static_cast<size_t>(i)];
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
