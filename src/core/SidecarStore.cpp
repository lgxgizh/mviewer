// M17: SidecarStore implementation — per-image .xmp sidecar file I/O.
#include "SidecarStore.h"
#include "RatingStore.h"

#include <algorithm>
#include <ctime>
#include <filesystem>
#include <fstream>
#include <sstream>
#include <system_error>

namespace mviewer::core {
namespace {

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
            case 'n': val += '\n'; break;
            case 'r': val += '\r'; break;
            case 't': val += '\t'; break;
            default: val += json[pos]; break;
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

}  // namespace

SidecarStore &SidecarStore::instance()
{
    static SidecarStore inst;
    return inst;
}

std::string SidecarStore::sidecarPath(const std::string &imagePath)
{
    // Replace extension with .xmp; for no extension, append .xmp.
    namespace fs = std::filesystem;
    fs::path p(imagePath);
    std::string stem = p.stem().string();
    // Build sidecar: same dir + stem + ".xmp"
    fs::path dir = p.parent_path();
    return (dir / (stem + ".xmp")).string();
}

std::string SidecarStore::toJson(const std::string &imagePath)
{
    auto &rs = RatingStore::instance();
    std::ostringstream os;
    os << "{\n";
    os << "  \"file\": "       << qs(imagePath)        << ",\n";
    os << "  \"rating\": "     << rs.rating(imagePath)  << ",\n";
    os << "  \"colorLabel\": " << rs.colorLabel(imagePath) << ",\n";
    os << "  \"picked\": "     << (rs.picked(imagePath) ? "true" : "false") << ",\n";
    os << "  \"rejected\": "   << (rs.rejected(imagePath) ? "true" : "false") << "\n";
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
    auto &rs = RatingStore::instance();
    // Only write sidecar if there's actual data to save.
    if (rs.rating(imagePath) == 0 && rs.colorLabel(imagePath) == 0 &&
        !rs.picked(imagePath) && !rs.rejected(imagePath))
    {
        return removeSidecar(imagePath);
    }

    const std::string spath = sidecarPath(imagePath);
    std::error_code ec;
    const auto dir = std::filesystem::path(spath).parent_path();
    if (!dir.empty())
        std::filesystem::create_directories(dir, ec);

    std::ofstream out(spath, std::ios::trunc);
    if (!out)
        return false;
    out << toJson(imagePath);
    return true;
}

bool SidecarStore::readSidecar(const std::string &imagePath)
{
    const std::string spath = sidecarPath(imagePath);
    std::ifstream in(spath);
    if (!in)
        return false;
    std::ostringstream ss;
    ss << in.rdbuf();
    return fromJson(ss.str(), imagePath);
}

bool SidecarStore::removeSidecar(const std::string &imagePath)
{
    std::error_code ec;
    return std::filesystem::remove(sidecarPath(imagePath), ec) || !ec;
}

int SidecarStore::importDirectory(const std::string &dirPath)
{
    int count = 0;
    std::error_code ec;
    for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec))
    {
        if (ec)
            break;
        if (entry.is_regular_file() && entry.path().extension() == ".xmp")
        {
            std::ifstream in(entry.path());
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
    }
    return count;
}

int SidecarStore::exportDirectory(const std::string &dirPath)
{
    int count = 0;
    auto &rs = RatingStore::instance();
    std::error_code ec;

    // Walk directory for all image files with known extensions
    const std::set<std::string> imgExts = {
        ".png", ".jpg", ".jpeg", ".bmp", ".webp",
        ".tiff", ".tif", ".gif", ".tga", ".ppm", ".pgm", ".exr", ".hdr",
        ".dds", ".psd", ".svg"
    };

    for (const auto &entry : std::filesystem::directory_iterator(dirPath, ec))
    {
        if (ec)
            break;
        if (!entry.is_regular_file())
            continue;
        const std::string ext = entry.path().extension().string();
        std::string lowerExt = ext;
        std::transform(lowerExt.begin(), lowerExt.end(), lowerExt.begin(),
                       [](char c) { return static_cast<char>(std::tolower(c)); });
        if (imgExts.count(lowerExt) == 0)
            continue;

        const std::string imgPath = entry.path().string();
        if (rs.rating(imgPath) > 0 || rs.colorLabel(imgPath) > 0 ||
            rs.picked(imgPath) || rs.rejected(imgPath))
        {
            if (writeSidecar(imgPath))
                ++count;
        }
    }
    return count;
}

}  // namespace mviewer::core
