#include "core/image/yuv/YuvFile.h"

#include "core/filesystem/Utf8Path.h"

#include <cstdint>
#include <filesystem>
#include <fstream>
#include <limits>
#include <memory>
#include <utility>

namespace mviewer::core::yuv
{
namespace
{

bool endsWithIgnoreCase(const std::string &path, const char *suffix)
{
    const std::size_t n = std::char_traits<char>::length(suffix);
    if (path.size() < n)
        return false;
    const std::size_t off = path.size() - n;
    for (std::size_t i = 0; i < n; ++i)
    {
        unsigned char a = static_cast<unsigned char>(path[off + i]);
        unsigned char b = static_cast<unsigned char>(suffix[i]);
        if (a >= 'A' && a <= 'Z')
            a = static_cast<unsigned char>(a - 'A' + 'a');
        if (b >= 'A' && b <= 'Z')
            b = static_cast<unsigned char>(b - 'A' + 'a');
        if (a != b)
            return false;
    }
    return true;
}

void splitPathNames(const std::string &path, std::string &fileName, std::string &folderName)
{
    const auto slash = path.find_last_of("/\\");
    if (slash == std::string::npos)
    {
        fileName = path;
        folderName.clear();
        return;
    }
    fileName = path.substr(slash + 1);
    if (slash == 0)
    {
        folderName.clear();
        return;
    }
    const std::string parent = path.substr(0, slash);
    const auto slash2 = parent.find_last_of("/\\");
    folderName = (slash2 == std::string::npos) ? parent : parent.substr(slash2 + 1);
}

std::size_t readAt(const std::shared_ptr<std::ifstream> &file, int64_t offset, std::uint8_t *buf,
                   std::size_t n)
{
    try
    {
        if (!file || buf == nullptr || n == 0 || offset < 0)
            return 0;
        if (offset > static_cast<int64_t>(std::numeric_limits<std::streamoff>::max()))
            return 0;
        file->clear();
        file->seekg(static_cast<std::streamoff>(offset), std::ios::beg);
        if (!file->good())
        {
            file->clear();
            return 0;
        }
        const auto maxAsk = static_cast<std::uint64_t>(std::numeric_limits<std::streamsize>::max());
        const std::streamsize ask = (static_cast<std::uint64_t>(n) > maxAsk)
                                        ? std::numeric_limits<std::streamsize>::max()
                                        : static_cast<std::streamsize>(n);
        file->read(reinterpret_cast<char *>(buf), ask);
        const auto got = file->gcount();
        if (!file->good())
            file->clear();
        if (got <= 0)
            return 0;
        return static_cast<std::size_t>(got);
    }
    catch (...)
    {
        return 0;
    }
}

std::string colorSpaceOf(const Format &format)
{
    const char *matrix = "BT.709";
    if (format.matrix == Matrix::BT601)
        matrix = "BT.601";
    else if (format.matrix == Matrix::BT2020)
        matrix = "BT.2020";
    const char *range = (format.range == Range::Full) ? "full" : "limited";
    std::string text = matrix;
    text.push_back(' ');
    text += range;
    return text;
}

int clampFrameCount(int64_t count)
{
    if (count < 1)
        return 1;
    const int64_t limit = static_cast<int64_t>(std::numeric_limits<int>::max());
    if (count > limit)
        return std::numeric_limits<int>::max();
    return static_cast<int>(count);
}

} // namespace

bool isYuvPath(const std::string &path)
{
    return endsWithIgnoreCase(path, ".yuv") || endsWithIgnoreCase(path, ".y4m");
}

std::string yuvParamKey(const std::string &path)
{
    return mviewer::core::normalizeKey(path);
}

bool openYuvFile(const std::string &path, OpenedYuvFile &out)
{
    out = OpenedYuvFile{};
    try
    {
        const auto native = mviewer::core::pathFromUtf8(path);
        auto file = std::make_shared<std::ifstream>(native, std::ios::binary);
        if (!file || !file->is_open())
        {
            out.info.known = false;
            out.info.why = "cannot open file";
            return false;
        }
        std::error_code ec;
        const auto sz = std::filesystem::file_size(native, ec);
        if (ec || sz > static_cast<std::uintmax_t>(std::numeric_limits<int64_t>::max()))
        {
            out.info.known = false;
            out.info.why = "cannot read file size";
            return false;
        }
        out.fileSize = static_cast<int64_t>(sz);
        std::string fileName;
        std::string folderName;
        splitPathNames(path, fileName, folderName);
        const std::shared_ptr<std::ifstream> opened = file;
        out.read = [opened](int64_t offset, std::uint8_t *buf, std::size_t n)
        { return readAt(opened, offset, buf, n); };
        const auto saved = YuvParamStore::instance().get(yuvParamKey(path));
        out.info = resolveYuvSource(fileName, folderName, out.fileSize, out.read, saved);
        return true;
    }
    catch (...)
    {
        out.read = ReadAtFn{};
        out.info.known = false;
        if (out.info.why.empty())
            out.info.why = "exception while opening file";
        return false;
    }
}

void fillYuvMetadata(const OpenedYuvFile &f, const std::string &path, int frameIndex,
                     mviewer::domain::ImageMetadata &meta)
{
    std::string fileName;
    std::string folderName;
    splitPathNames(path, fileName, folderName);
    meta.width = f.info.format.width;
    meta.height = f.info.format.height;
    meta.fileSize = f.fileSize;
    meta.fileName = std::move(fileName);
    meta.filePath = path;
    meta.bitDepth = f.info.format.bitDepth;
    meta.channels = (f.info.format.subsampling == Subsampling::S400) ? 1 : 3;
    meta.format = f.info.isY4m ? "Y4M" : "YUV";
    meta.colorSpace = colorSpaceOf(f.info.format);
    meta.frameCount = clampFrameCount(f.info.frameCount);
    meta.currentFrame = frameIndex;
    meta.sequenceKind = (meta.frameCount > 1) ? "pages" : "static";
    meta.animated = false;
}

std::string yuvCacheKeySuffix(const std::string &path)
{
    try
    {
        const auto saved = YuvParamStore::instance().get(yuvParamKey(path));
        if (!saved.has_value())
            return {};
        return std::string("|yuv=") + toString(*saved);
    }
    catch (...)
    {
        return {};
    }
}

bool yuvCacheKeyAgrees(const std::string &key, const std::string &suffix)
{
    const std::string marker = "|yuv=";
    if (suffix.empty())
        return key.find(marker) == std::string::npos;
    if (key.size() < suffix.size())
        return false;
    return key.compare(key.size() - suffix.size(), suffix.size(), suffix) == 0;
}

} // namespace mviewer::core::yuv
