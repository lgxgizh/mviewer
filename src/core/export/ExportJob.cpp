#include "core/export/ExportJob.h"
#include "core/export/ExportJobInternal.h"

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

ImageData maybeResize(const ImageData &d, const ExportJobConfig &cfg)
{
    if (cfg.resizeMode == ResizeMode::Fit)
        return mviewer::core::resizeToFit(d, cfg.resizeValue, cfg.resizeValue);
    if (cfg.resizeMode == ResizeMode::Scale)
        return mviewer::core::resizeByFactor(d, cfg.resizeValue / 100.0);
    return d;
}

ImageData maybeWatermark(const ImageData &d, const ExportJobConfig &cfg)
{
    if (cfg.watermarkText.empty())
        return d;
    const auto pos = static_cast<mviewer::core::WatermarkPosition>(cfg.watermarkPos);
    return mviewer::core::addTextWatermark(d, cfg.watermarkText, pos, cfg.watermarkOpacity / 100.0,
                                           32);
}

std::string extensionFor(const std::string &format)
{
    if (format == "jpeg")
        return ".jpg";
    return "." + format;
}

ImageData decodeSource(const std::string &path, const ExportJobConfig &cfg)
{
    if (!cfg.preserveDisplayAppearance)
        return Decoder::decodeFull(path);

    // The repository path preserves the viewer's decode + metadata contract;
    // the ICC/display conversion is deliberately performed on this worker,
    // never by a QAction, QWidget, or clipboard callback on the GUI thread.
    const ImageLoadOptions opts{true, false, 256};
    const auto loaded = ImageRepository::instance().load(path, opts);
    if (!loaded.success() || !loaded.frame)
        return {};
    return mvcore::toDisplayImageData(loaded.frame->pixels(), loaded.frame->metadata());
}

bool writeTextAtomically(const std::string &destination, const std::string &contents)
{
    try
    {
        return writeTextAtomically(pathFromUtf8(destination), contents);
    }
    catch (const std::exception &)
    {
        return false;
    }
}

fs::path pathFromUtf8(const std::string &value);
std::string pathToUtf8(const fs::path &path);
std::string csvEscape(const std::string &value)
{
    if (value.find_first_of(",\"\r\n") == std::string::npos)
        return value;
    std::string out = "\"";
    for (const char c : value)
        out += c == '"' ? "\"\"" : std::string(1, c);
    out += '"';
    return out;
}

std::string jsonEscape(const std::string &value)
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

std::string htmlEscape(const std::string &value)
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

std::string fixedNumber(double value)
{
    if (!std::isfinite(value))
        return {};
    std::ostringstream out;
    out << std::fixed << std::setprecision(2) << value;
    return out.str();
}

std::string jsonNumber(double value)
{
    const std::string number = fixedNumber(value);
    return number.empty() ? "null" : number;
}

std::optional<ReportRow> analyzeSource(const fs::path &path)
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

bool writeTextAtomically(const fs::path &destination, const std::string &contents)
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

fs::path pathFromUtf8(const std::string &value)
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

std::string pathToUtf8(const fs::path &path)
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

std::string outputBaseName(const ExportJobConfig &cfg, const fs::path &source, int index,
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


} // namespace mviewer::exportjob
