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

std::string readWholeFile(const std::filesystem::path &path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in)
        return {};
    std::ostringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

std::string lowerAscii(std::string text)
{
    std::transform(text.begin(), text.end(), text.begin(),
                   [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return text;
}

bool isImagePath(const std::filesystem::path &p)
{
    return mviewer::core::ImageFormats::isSupportedSuffix(lowerAscii(pathToUtf8(p.extension())));
}

// True when another supported image in the same directory shares the stem
// of `image` (IMG_001.jpg vs IMG_001.png), i.e. a legacy stem.xmp sidecar
// cannot be attributed to one of them without its "file" record.
bool hasStemSibling(const std::filesystem::path &image)
{
    std::error_code ec;
    std::filesystem::directory_iterator it(
        image.parent_path(), std::filesystem::directory_options::skip_permission_denied, ec);
    for (std::filesystem::directory_iterator end; !ec && it != end; it.increment(ec))
    {
        const auto &candidate = it->path();
        if (candidate.filename() == image.filename() || candidate.stem() != image.stem())
            continue;
        std::error_code fileEc;
        if (it->is_regular_file(fileEc) && isImagePath(candidate))
            return true;
    }
    return false;
}

} // namespace

SidecarStore &SidecarStore::instance()
{
    static SidecarStore inst;
    return inst;
}

std::string SidecarStore::sidecarPath(const std::string &imagePath)
{
    if (imagePath.empty())
        return {};
    return imagePath + ".xmp";
}

std::string SidecarStore::legacySidecarPath(const std::string &imagePath)
{
    std::filesystem::path p = pathFromUtf8(imagePath);
    if (!p.has_extension())
        return {};
    p.replace_extension(pathFromUtf8(".xmp"));
    return pathToUtf8(p);
}

std::string SidecarStore::ownedLegacySidecar(const std::string &imagePath)
{
    std::string legacy = legacySidecarPath(imagePath);
    if (legacy.empty())
        return {};
    std::error_code ec;
    if (!std::filesystem::is_regular_file(pathFromUtf8(legacy), ec))
        return {};
    const auto image = pathFromUtf8(imagePath);
    const std::string recorded = extractStr(readWholeFile(pathFromUtf8(legacy)), "file");
    if (!recorded.empty())
        return pathFromUtf8(recorded).filename() == image.filename() ? legacy : std::string{};
    return hasStemSibling(image) ? std::string{} : legacy;
}

std::string SidecarStore::existingSidecar(const std::string &imagePath)
{
    std::string current = sidecarPath(imagePath);
    std::error_code ec;
    if (!current.empty() && std::filesystem::is_regular_file(pathFromUtf8(current), ec))
        return current;
    return ownedLegacySidecar(imagePath);
}

bool SidecarStore::relocateSidecar(const std::string &oldImagePath, const std::string &newImagePath)
{
    try
    {
        const std::string source = existingSidecar(oldImagePath);
        if (source.empty() || oldImagePath == newImagePath)
            return false;
        fromJson(readWholeFile(pathFromUtf8(source)), newImagePath);
        // Write the destination sidecar BEFORE removing the source so a failed
        // write never deletes the only on-disk copy of the ratings.
        if (!writeSidecar(newImagePath))
            return false;
        const std::string destination = sidecarPath(newImagePath);
        if (!destination.empty() && source != destination)
        {
            std::error_code ec;
            std::filesystem::remove(pathFromUtf8(source), ec);
        }
        return true;
    }
    catch (...)
    {
        return false;
    }
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
        if (!atomicWriteFile(spath, toJson(imagePath), &error))
            return false;
        // Migrate: once the per-file sidecar exists, an owned legacy
        // stem.xmp is redundant.
        const std::string legacy = ownedLegacySidecar(imagePath);
        if (!legacy.empty())
        {
            std::error_code ec;
            std::filesystem::remove(pathFromUtf8(legacy), ec);
        }
        return true;
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
        const std::string spath = existingSidecar(imagePath);
        if (spath.empty())
            return false;
        const bool ok = fromJson(readWholeFile(pathFromUtf8(spath)), imagePath);
        // Legacy stem.xmp that belongs to this image: migrate it to the
        // per-file name so a later same-stem sibling cannot claim it.
        if (ok && spath != sidecarPath(imagePath))
            writeSidecar(imagePath);
        return ok;
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
        const std::string legacy = ownedLegacySidecar(imagePath);
        if (!legacy.empty())
            std::filesystem::remove(pathFromUtf8(legacy), ec);
        ec.clear();
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
