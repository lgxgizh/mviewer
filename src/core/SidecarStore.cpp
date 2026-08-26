// M17: SidecarStore implementation — per-image .xmp sidecar file I/O.
#include "SidecarStore.h"
#include "RatingStore.h"
#include "core/filesystem/AtomicFile.h"
#include "core/filesystem/Utf8Path.h"
#include "core/image/ImageFormats.h"
#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace mviewer::core
{
namespace
{

// Lightweight JSON string quoting.
std::string qs(const std::string &v)
{
    std::ostringstream os;
    os << '"';
    for (char c : v)
    {
        if (c == '"' || c == '\\')
            os << '\\' << c;
        else if (c == '\n')
            os << "\\n";
        else if (c == '\t')
            os << "\\t";
        else
            os << c;
    }
    os << '"';
    return os.str();
}

std::string extractStr(const std::string &json, const std::string &key)
{
    const std::string target = '"' + key + '"';
    auto pos = json.find(target);
    if (pos == std::string::npos)
        return {};
    pos += target.size();
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' '))
        ++pos;
    if (pos >= json.size() || json[pos] != '"')
        return {};
    ++pos;
    std::string val;
    while (pos < json.size() && json[pos] != '"')
    {
        if (json[pos] == '\\' && pos + 1 < json.size())
        {
            ++pos;
            switch (json[pos])
            {
            case 'n':
                val += '\n';
                break;
            case 'r':
                val += '\r';
                break;
            case 't':
                val += '\t';
                break;
            default:
                val += json[pos];
                break;
            }
        }
        else
            val += json[pos];
        ++pos;
    }
    return val;
}

int extractInt(const std::string &json, const std::string &key, int fallback = 0)
{
    const std::string target = '"' + key + '"';
    auto pos = json.find(target);
    if (pos == std::string::npos)
        return fallback;
    pos += target.size();
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' '))
        ++pos;
    if (pos >= json.size())
        return fallback;
    auto end = pos;
    while (end < json.size() && (std::isdigit(json[end]) || json[end] == '-'))
        ++end;
    try
    {
        return std::stoi(json.substr(pos, end - pos));
    }
    catch (...)
    {
        return fallback;
    }
}

bool extractBool(const std::string &json, const std::string &key, bool fallback = false)
{
    const std::string target = '"' + key + '"';
    auto pos = json.find(target);
    if (pos == std::string::npos)
        return fallback;
    pos += target.size();
    while (pos < json.size() && (json[pos] == ':' || json[pos] == ' '))
        ++pos;
    if (pos >= json.size())
        return fallback;
    if (json.compare(pos, 4, "true") == 0)
        return true;
    if (json.compare(pos, 5, "false") == 0)
        return false;
    return fallback;
}

} // namespace

SidecarStore &SidecarStore::instance()
{
    static SidecarStore inst;
    return inst;
}

std::string SidecarStore::sidecarPath(const std::string &imagePath)
{
    // Replace extension with .xmp; for no extension, append .xmp.
    namespace fs = std::filesystem;
    fs::path p = pathFromUtf8(imagePath);
    p.replace_extension(pathFromUtf8(".xmp"));
    return pathToUtf8(p);
}

std::string SidecarStore::toJson(const std::string &imagePath)
{
    auto &rs = RatingStore::instance();
    std::ostringstream os;
    os << "{\n";
    os << "  \"file\": " << qs(imagePath) << ",\n";
    os << "  \"rating\": " << rs.rating(imagePath) << ",\n";
    os << "  \"colorLabel\": " << rs.colorLabel(imagePath) << ",\n";
    os << "  \"picked\": " << (rs.picked(imagePath) ? "true" : "false") << ",\n";
    os << "  \"rejected\": " << (rs.rejected(imagePath) ? "true" : "false") << "\n";
    os << "}\n";
    return os.str();
}

bool SidecarStore::fromJson(const std::string &json, const std::string &imagePath)
{
    const int rating = extractInt(json, "rating", 0);
    const int colorLabel = extractInt(json, "colorLabel", 0);
    const bool picked = extractBool(json, "picked", false);
    const bool rejected = extractBool(json, "rejected", false);

    auto &rs = RatingStore::instance();

    // Only update if the sidecar has data (non-zero rating or non-zero label)
    // This prevents overwriting newer in-store data with empty sidecar files.
    if (rating >= 1 && rating <= 5)
        rs.setRating(imagePath, rating);
    if (colorLabel >= 1 && colorLabel <= 6)
        rs.setColorLabel(imagePath, colorLabel);
    if (picked)
        rs.setPicked(imagePath, true);
    if (rejected)
        rs.setRejected(imagePath, true);

    return true;
}

bool SidecarStore::writeSidecar(const std::string &imagePath)
{
    try
    {
    auto &rs = RatingStore::instance();
    // Only write sidecar if there's actual data to save.
    if (rs.rating(imagePath) == 0 && rs.colorLabel(imagePath) == 0 && !rs.picked(imagePath) &&
        !rs.rejected(imagePath))
    {
        return removeSidecar(imagePath);
    }

    const std::string spath = sidecarPath(imagePath);
    if (spath.empty())
        return false;
    // M46: crash-safe atomic replace — a failed sidecar write leaves the
    // previous .xmp intact and can never leave a half-written JSON file.
    std::string error;
    return atomicWriteFile(spath, toJson(imagePath), &error);
    }
    catch (...)
    {
        return false;
    }
}

bool SidecarStore::readSidecar(const std::string &imagePath)
{
    try
    {
        const std::string spath = sidecarPath(imagePath);
        std::ifstream in(pathFromUtf8(spath), std::ios::binary);
        if (!in)
            return false;
        std::ostringstream ss;
        ss << in.rdbuf();
        return fromJson(ss.str(), imagePath);
    }
    catch (...)
    {
        return false;
    }
}

bool SidecarStore::removeSidecar(const std::string &imagePath)
{
    try
    {
        std::error_code ec;
        return std::filesystem::remove(pathFromUtf8(sidecarPath(imagePath)), ec) || !ec;
    }
    catch (...)
    {
        return false;
    }
}

int SidecarStore::importDirectory(const std::string &dirPath,
                                  const std::function<bool()> &cancelled)
{
    try
    {
        int count = 0;
        std::error_code ec;
        const auto dir = pathFromUtf8(dirPath);
        std::filesystem::directory_iterator it(
            dir, std::filesystem::directory_options::skip_permission_denied, ec);
        for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
        {
            if (cancelled && cancelled())
                break;
            const auto &entry = *it;
            std::error_code fileEc;
            if (!entry.is_regular_file(fileEc))
                continue;
            std::string ext = pathToUtf8(entry.path().extension());
            std::transform(ext.begin(), ext.end(), ext.begin(),
                           [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (ext != ".xmp")
                continue;
            std::ifstream in(entry.path(), std::ios::binary);
            if (!in)
                continue;
            std::ostringstream ss;
            ss << in.rdbuf();
            const std::string json = ss.str();
            const std::string imageFile = extractStr(json, "file");
            if (!imageFile.empty())
            {
                fromJson(json, imageFile);
                ++count;
            }
        }
        return count;
    }
    catch (...)
    {
        return 0;
    }
}

int SidecarStore::exportDirectory(const std::string &dirPath)
{
    try
    {
        int count = 0;
        auto &rs = RatingStore::instance();
        std::error_code ec;

    // Walk directory for all image files with known extensions (M25: the
    // shipped-format SSOT decides what counts as an image).
    auto isImageFile = [](const std::filesystem::path &p)
    {
        const std::string ext = pathToUtf8(p.extension());
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                       [](char c) { return static_cast<char>(std::tolower(c)); });
        return mviewer::core::ImageFormats::isSupportedSuffix(lowerExt);
    };

    const auto dir = pathFromUtf8(dirPath);
    std::filesystem::directory_iterator it(
        dir, std::filesystem::directory_options::skip_permission_denied, ec);
    for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
    {
        const auto &entry = *it;
        std::error_code fileEc;
        if (!entry.is_regular_file(fileEc))
            continue;
        if (!isImageFile(entry.path()))
            continue;

        const std::string imgPath = pathToUtf8(entry.path());
        if (rs.rating(imgPath) > 0 || rs.colorLabel(imgPath) > 0 || rs.picked(imgPath) ||
            rs.rejected(imgPath))
        {
            if (writeSidecar(imgPath))
                ++count;
        }
    }
        return count;
    }
    catch (...)
    {
        return 0;
    }
}

} // namespace mviewer::core
